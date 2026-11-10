// template.cpp — date, counter and metadata template expansion (design §3.7).
//
// The pattern set and the collision fixes are borrowed from PowerRename (RNF-21,
// RNF-22) because they encode a decade of user expectations:
//
//   * Longer patterns must be tried before shorter ones, or "$YYYY" would be
//     consumed as "$Y" + "YYY".
//   * "$D" must not swallow "$DATE_TAKEN_*", "$DESCRIPTION" or "$DOCUMENT_ID".
//   * "$H" must not swallow "$HEIGHT".
//   * "$$" is an escaped literal dollar and must win over everything.
//
// A pattern with no value available keeps its literal text rather than becoming
// empty (RNF-23). Silently deleting user text is the one failure mode users
// cannot diagnose.
#include "core/rename_core.h"

#include <array>
#include <cstdio>
#include <cwchar>

namespace rename_core {

namespace {

struct PatternSite {
    const wchar_t* pattern;
    TemplateField field;
};

// Longest first within each family. kPatterns order *is* the precedence rule;
// no length sorting happens at runtime.
constexpr std::array<PatternSite, 20> kPatterns = {{
    {L"YYYY", TemplateField::Year4},
    {L"MMMM", TemplateField::MonthLong},
    {L"MMM",  TemplateField::MonthShort},
    {L"MM",   TemplateField::Month2},
    {L"DDDD", TemplateField::WeekdayLong},
    {L"DDD",  TemplateField::WeekdayShort},
    {L"DD",   TemplateField::Day2},
    {L"HH",   TemplateField::Hour12_2},
    {L"hh",   TemplateField::Hour24_2},
    {L"TT",   TemplateField::AmPmUpper},
    {L"tt",   TemplateField::AmPmLower},
    {L"mm",   TemplateField::Minute2},
    {L"ss",   TemplateField::Second2},
    {L"fff",  TemplateField::Milli3},
    {L"ff",   TemplateField::Milli2},
    {L"YY",   TemplateField::Year2},
    {L"Y",    TemplateField::Year1},
    {L"M",    TemplateField::Month1},
    {L"D",    TemplateField::Day1},
    {L"H",    TemplateField::Hour12_1},
}};

// Prefixes that begin a longer metadata pattern. When "$D" is followed by one of
// these it is part of that pattern instead of a date field. "ATE_TAKEN" without a
// trailing underscore is included because that is itself a valid metadata key.
constexpr std::array<std::wstring_view, 4> kDsuffixes = {
    L"ATE_TAKEN_", L"ATE_TAKEN", L"ESCRIPTION", L"OCUMENT_ID",
};

std::wstring FieldText(TemplateField field, const TemplateContext& ctx)
{
    const DateParts& d = ctx.date;
    wchar_t buffer[64]{};

    switch (field)
    {
    case TemplateField::Year4:        std::swprintf(buffer, 64, L"%04d", d.year); break;
    case TemplateField::Year2:        std::swprintf(buffer, 64, L"%02d", d.year % 100); break;
    case TemplateField::Year1:        std::swprintf(buffer, 64, L"%d", d.year % 10); break;
    case TemplateField::MonthLong:    return ctx.monthLong;
    case TemplateField::MonthShort:   return ctx.monthShort;
    case TemplateField::Month2:       std::swprintf(buffer, 64, L"%02d", d.month); break;
    case TemplateField::Month1:       std::swprintf(buffer, 64, L"%d", d.month); break;
    case TemplateField::WeekdayLong:  return ctx.weekdayLong;
    case TemplateField::WeekdayShort: return ctx.weekdayShort;
    case TemplateField::Day2:         std::swprintf(buffer, 64, L"%02d", d.day); break;
    case TemplateField::Day1:         std::swprintf(buffer, 64, L"%d", d.day); break;
    case TemplateField::Hour12_2:     std::swprintf(buffer, 64, L"%02d", d.hour12()); break;
    case TemplateField::Hour12_1:     std::swprintf(buffer, 64, L"%d", d.hour12()); break;
    case TemplateField::Hour24_2:     std::swprintf(buffer, 64, L"%02d", d.hour); break;
    case TemplateField::Minute2:      std::swprintf(buffer, 64, L"%02d", d.minute); break;
    case TemplateField::Second2:      std::swprintf(buffer, 64, L"%02d", d.second); break;
    case TemplateField::Milli3:       std::swprintf(buffer, 64, L"%03d", d.millisecond); break;
    case TemplateField::Milli2:       std::swprintf(buffer, 64, L"%02d", d.millisecond / 10); break;
    case TemplateField::AmPmUpper:    return d.hour < 12 ? L"AM" : L"PM";
    case TemplateField::AmPmLower:    return d.hour < 12 ? L"am" : L"pm";
    }
    return std::wstring(buffer);
}

// Parse "start=N,increment=N,padding=N" out of a counter body. Unknown keys are
// ignored; a malformed number keeps the default, because a counter that silently
// becomes 0 is worse than one that keeps its documented default.
CounterState ParseCounterBody(std::wstring_view body, const CounterState& base)
{
    CounterState out = base;

    auto readNumber = [&body](std::wstring_view key, long long& target) {
        const std::size_t at = body.find(key);
        if (at == std::wstring_view::npos)
            return;
        std::size_t cursor = at + key.size();
        bool negative = false;
        if (cursor < body.size() && (body[cursor] == L'-' || body[cursor] == L'+'))
        {
            negative = body[cursor] == L'-';
            ++cursor;
        }
        long long value = 0;
        bool any = false;
        while (cursor < body.size() && body[cursor] >= L'0' && body[cursor] <= L'9')
        {
            value = value * 10 + (body[cursor] - L'0');
            ++cursor;
            any = true;
        }
        if (any)
            target = negative ? -value : value;
    };

    long long start = out.start;
    long long increment = out.increment;
    long long padding = static_cast<long long>(out.padding);
    readNumber(L"start=", start);
    readNumber(L"increment=", increment);
    readNumber(L"padding=", padding);

    out.start = start;
    out.increment = increment;
    out.padding = padding > 0 ? static_cast<unsigned>(padding) : 0u;
    return out;
}

} // namespace

int DateParts::hour12() const
{
    const int h = hour % 12;
    return h == 0 ? 12 : h;
}

std::wstring ExpandTemplate(std::wstring_view input, const TemplateContext& ctx)
{
    std::wstring out;
    out.reserve(input.size());

    std::size_t i = 0;
    while (i < input.size())
    {
        if (input[i] != L'$')
        {
            out.push_back(input[i]);
            ++i;
            continue;
        }

        // "$$" is a literal dollar and wins over every pattern.
        if (i + 1 < input.size() && input[i + 1] == L'$')
        {
            out.push_back(L'$');
            i += 2;
            continue;
        }

        // Counter: "${start=1,increment=1,padding=3}".
        if (i + 1 < input.size() && input[i + 1] == L'{')
        {
            const std::size_t close = input.find(L'}', i + 2);
            if (close != std::wstring_view::npos)
            {
                const std::wstring_view body = input.substr(i + 2, close - (i + 2));
                out.append(FormatCounter(ParseCounterBody(body, ctx.counter)));
                i = close + 1;
                continue;
            }
            // Unterminated brace: emit literally rather than eating the rest.
            out.push_back(L'$');
            ++i;
            continue;
        }

        // Date field; kPatterns order gives longest-match precedence.
        bool matched = false;
        for (const PatternSite& site : kPatterns)
        {
            const std::size_t len = std::wcslen(site.pattern);
            if (i + 1 + len > input.size() || input.compare(i + 1, len, site.pattern) != 0)
                continue;

            // "$D" must not eat "$DATE_TAKEN_*" and friends.
            if (site.field == TemplateField::Day1)
            {
                const std::wstring_view tail = input.substr(i + 2);
                bool metadataPrefix = false;
                for (std::wstring_view suffix : kDsuffixes)
                {
                    if (tail.size() >= suffix.size() && tail.compare(0, suffix.size(), suffix) == 0)
                    {
                        metadataPrefix = true;
                        break;
                    }
                }
                if (metadataPrefix)
                    continue;
            }
            // "$H" must not eat "$HEIGHT".
            if (site.field == TemplateField::Hour12_1)
            {
                const std::wstring_view tail = input.substr(i + 2);
                if (tail.size() >= 5 && tail.compare(0, 5, L"EIGHT") == 0)
                    continue;
            }

            out.append(FieldText(site.field, ctx));
            i += 1 + len;
            matched = true;
            break;
        }
        if (matched)
            continue;

        // Metadata placeholder: match the longest key the context actually has, so
        // "$DATE_TAKEN_YYYY" is not consumed as "$DATE_TAKEN". A key that is known
        // by name but absent from the map keeps its literal form (RNF-23).
        {
            std::wstring_view best;
            std::wstring_view bestValue;
            for (const auto& [name, value] : ctx.metadata)
            {
                if (name.empty() || i + 1 + name.size() > input.size())
                    continue;
                if (input.compare(i + 1, name.size(), name) != 0)
                    continue;
                if (best.empty() || name.size() > best.size())
                {
                    best = name;
                    bestValue = value;
                }
            }
            if (!best.empty())
            {
                out.append(bestValue);
                i += 1 + best.size();
                continue;
            }
        }

        // Unknown sequence: keep it literal. A trailing bare "$" also lands here.
        out.push_back(L'$');
        ++i;
    }

    return out;
}

} // namespace rename_core
