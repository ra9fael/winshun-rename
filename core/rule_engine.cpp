// rule_engine.cpp — single-step replacement, rule chains, and item evaluation
// (design §3.3, §3.5, §3.7, §4.1).
//
// Three behaviours here are the ones that get quietly wrong and then produce
// plausible-looking wrong names:
//
//   1. Scope splitting happens on *the previous rule's output*, not on the
//      original leaf. A chain "full -> extension" must re-split after the first
//      rule changed the name.
//   2. An extension-only rule on an extensionless file is skipped with an
//      explanation, never run against an empty string a regex could match.
//   3. The counter is shared across the whole batch and advances only when a
//      chain matched at least once on that item, so unmatched rows consume no
//      numbers (RNF-16). Our addition: two templates in one chain share the same
//      value.
#include "core/rename_core.h"

#include <cwctype>
#include <regex>

namespace rename_core {

namespace {

std::wstring ToLowerCopy(std::wstring_view text)
{
    std::wstring out(text);
    for (wchar_t& ch : out)
        ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    return out;
}

// Literal search/replace, honouring case sensitivity and the first/all policy.
ReplaceResult ReplaceLiteral(std::wstring_view source,
                            const Rule& rule,
                            const TemplateContext& ctx)
{
    ReplaceResult out;
    out.text.assign(source);

    if (rule.search.empty())
        return out;

    const bool caseInsensitive = (rule.flags & FlagCaseSensitive) == 0;
    const bool matchAll = rule.occurrence == Occurrence::All;

    // Fold only for searching; replacements always splice into the original so
    // the user's casing survives.
    const std::wstring haystack = caseInsensitive ? ToLowerCopy(source) : std::wstring(source);
    const std::wstring needle = caseInsensitive ? ToLowerCopy(rule.search) : rule.search;

    std::wstring built;
    std::size_t cursor = 0;

    for (;;)
    {
        const std::size_t hit = haystack.find(needle, cursor);
        if (hit == std::wstring::npos)
            break;

        built.append(source.substr(cursor, hit - cursor));
        built.append(ExpandTemplate(rule.replace, ctx));
        out.hits.push_back({hit, needle.size()});
        out.matched = true;
        cursor = hit + needle.size();

        if (!matchAll)
            break;
    }

    built.append(source.substr(cursor));
    out.text = std::move(built);
    return out;
}

ReplaceResult ReplaceRegex(std::wstring_view source,
                          const Rule& rule,
                          const TemplateContext& ctx)
{
    ReplaceResult out;
    out.text.assign(source);

    if (rule.search.empty())
        return out;

    const bool caseInsensitive = (rule.flags & FlagCaseSensitive) == 0;

    try
    {
        auto flags = std::regex_constants::ECMAScript;
        if (caseInsensitive)
            flags |= std::regex_constants::icase;

        const std::wregex pattern(rule.search, flags);
        const std::wstring replacement = ExpandTemplate(rule.replace, ctx);

        // Collect match spans first: they drive the preview highlight, and a
        // std::regex_iterator walk is cheaper than a second pass. The iterator
        // needs real iterators into an owned string, hence the local copy.
        const std::wstring sourceCopy(source);
        for (auto it = std::wsregex_iterator(sourceCopy.begin(), sourceCopy.end(), pattern);
             it != std::wsregex_iterator();
             ++it)
        {
            out.hits.push_back({static_cast<std::size_t>(it->position()),
                               static_cast<std::size_t>(it->length())});
            out.matched = true;
        }

        if (!out.matched)
            return out;

        const auto format = (rule.occurrence == Occurrence::All)
                                ? std::regex_constants::match_default
                                : std::regex_constants::format_first_only;
        out.text = std::regex_replace(sourceCopy, pattern, replacement, format);
    }
    catch (const std::regex_error&)
    {
        // A bad pattern must not become a partially-renamed file.
        out.error = RuleError::BadRegex;
        out.text.assign(source);
        out.hits.clear();
        out.matched = false;
    }
    return out;
}

} // namespace

ReplaceResult Replace(std::wstring_view source, const Rule& rule, const TemplateContext& ctx)
{
    if (rule.error != RuleError::None)
    {
        ReplaceResult out;
        out.text.assign(source);
        out.error = rule.error;
        return out;
    }

    return rule.match == MatchType::Regex ? ReplaceRegex(source, rule, ctx)
                                          : ReplaceLiteral(source, rule, ctx);
}

ItemOutcome Evaluate(const ItemSnapshot& item,
                     const RuleChain& chain,
                     std::uint32_t /*flags*/,
                     CounterState& counter)
{
    ItemOutcome out;
    out.originalLeaf = item.leaf;
    out.isFolder = item.isFolder;

    std::wstring current = item.leaf;
    bool anyMatched = false;
    bool skippedExtensionRule = false;

    for (const Rule& rule : chain.rules)
    {
        if (!rule.enabled)
            continue;

        // A directory has no extension, so an extension rule cannot apply. Same
        // rule for an extensionless file: report it instead of running the
        // pattern against "" where a regex could invent a suffix.
        if (rule.scope == Scope::Extension && (item.isFolder || !Split(current).hasExtension))
        {
            skippedExtensionRule = true;
            continue;
        }

        const ScopePortion portion = Portion(current, rule.scope);
        if (!portion.applies)
        {
            skippedExtensionRule = true;
            continue;
        }

        TemplateContext ctx = item.templateContext;
        // Every template in this item's chain sees the same counter value, so two
        // "${}" in one rule print the same number (design §3.7).
        ctx.counter.value = counter.value;

        const ReplaceResult replaced = Replace(portion.text, rule, ctx);
        if (replaced.error != RuleError::None)
        {
            out.error = replaced.error;
            out.targetLeaf = current;
            return out;
        }

        if (!replaced.matched)
            continue;

        anyMatched = true;

        // Re-join according to the scope that was edited.
        switch (rule.scope)
        {
        case Scope::Full:
            current = replaced.text;
            break;
        case Scope::Name:
        {
            const SplitName parts = Split(current);
            current = Join(replaced.text, parts.extension, parts.hasExtension);
            break;
        }
        case Scope::Extension:
        {
            const SplitName parts = Split(current);
            current = Join(parts.stem, replaced.text, true);
            break;
        }
        }
    }

    // Advance the shared counter only if this item's chain matched at all.
    if (anyMatched)
        counter.value += counter.increment;

    out.targetLeaf = std::move(current);
    out.skippedExtensionRule = skippedExtensionRule;
    return out;
}

} // namespace rename_core
