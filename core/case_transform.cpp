// case_transform.cpp — the four transforms (design §3.6, RNF-17).
//
// Title Case is the only subtle one: English title rules keep short articles,
// conjunctions and prepositions lowercase unless they start or end the phrase,
// and an apostrophe inside a contraction ("don't") must not start a new word.
// PowerRename ships exactly such a list, so we follow it rather than inventing
// our own — the point is that users get the conventional result.
#include "core/rename_core.h"

#include <algorithm>
#include <array>
#include <cwctype>

namespace rename_core {

namespace {

bool IsWordSeparator(wchar_t ch)
{
    return std::iswspace(static_cast<wint_t>(ch)) != 0
        || std::iswpunct(static_cast<wint_t>(ch)) != 0;
}

// An apostrophe inside a word is not a boundary: "don't" is one word, so the "t"
// must not be capitalised into "Don'T". Only a leading quote (at the very start,
// or right after a separator) counts as punctuation.
bool IsSeparatorAt(std::wstring_view text, std::size_t index)
{
    const wchar_t ch = text[index];
    if (ch == L'\'' || ch == L'\u2019')
    {
        if (index == 0)
            return true;                                  // opening quote
        return IsWordSeparator(text[index - 1]);          // closing quote
    }
    return IsWordSeparator(ch);
}

// A lowercased word that keeps its original case in Title Case, except at the
// start or the end of the phrase.
bool IsExceptionWord(const std::wstring& word)
{
    static constexpr std::array<std::wstring_view, 15> kExceptions = {
        L"a", L"an", L"and", L"as", L"at", L"but", L"by",
        L"for", L"in", L"nor", L"of", L"on", L"or", L"the", L"to",
    };
    for (std::wstring_view candidate : kExceptions)
    {
        if (word == candidate)
            return true;
    }
    return false;
}

wchar_t Upper(wchar_t ch) { return static_cast<wchar_t>(std::towupper(static_cast<wint_t>(ch))); }
wchar_t Lower(wchar_t ch) { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch))); }

std::wstring ToUpper(std::wstring_view text)
{
    std::wstring out(text);
    std::transform(out.begin(), out.end(), out.begin(), Upper);
    return out;
}

std::wstring ToLower(std::wstring_view text)
{
    std::wstring out(text);
    std::transform(out.begin(), out.end(), out.begin(), Lower);
    return out;
}

// Capitalize the first letter of each word, lowercasing the rest of the word.
// "hello WORLD" -> "Hello World".
std::wstring Capitalize(std::wstring_view text)
{
    std::wstring out(text);
    bool atWordStart = true;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        if (IsSeparatorAt(out, i))
        {
            atWordStart = true;
            continue;
        }
        wchar_t& ch = out[i];
        ch = atWordStart ? Upper(ch) : Lower(ch);
        atWordStart = false;
    }
    return out;
}

// Title Case: capitalize each word, but keep exception words lowercase unless
// they open or close the phrase, and do not treat an apostrophe inside a word as
// a separator.
std::wstring TitleCase(std::wstring_view text)
{
    std::wstring out = ToLower(text);

    // Collect [start, end) spans of words so "is this the last word" is cheap.
    std::vector<std::pair<std::size_t, std::size_t>> words;
    std::size_t i = 0;
    while (i < out.size())
    {
        while (i < out.size() && IsSeparatorAt(out, i))
            ++i;
        if (i >= out.size())
            break;
        const std::size_t start = i;
        while (i < out.size() && !IsSeparatorAt(out, i))
            ++i;
        words.emplace_back(start, i);
    }

    for (std::size_t w = 0; w < words.size(); ++w)
    {
        const auto [start, end] = words[w];
        const bool isFirst = (w == 0);
        const bool isLast = (w + 1 == words.size());
        const std::wstring word = out.substr(start, end - start);

        if (!isFirst && !isLast && IsExceptionWord(word))
            continue;   // already lowercased

        out[start] = Upper(out[start]);
    }

    return out;
}

} // namespace

std::wstring ApplyCase(std::wstring_view text, std::uint32_t flags)
{
    // The four transforms are mutually exclusive in the UI. If several were set
    // by a malformed preset, precedence follows the flag order so the result is
    // still deterministic rather than "whichever branch ran".
    if ((flags & FlagUppercase) != 0)
        return ToUpper(text);
    if ((flags & FlagLowercase) != 0)
        return ToLower(text);
    if ((flags & FlagTitleCase) != 0)
        return TitleCase(text);
    if ((flags & FlagCapitalize) != 0)
        return Capitalize(text);
    return std::wstring(text);
}

} // namespace rename_core
