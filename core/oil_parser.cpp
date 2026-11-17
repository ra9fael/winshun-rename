// oil_parser.cpp — parse the text buffer into per-line records (design §3.8).
//
// The number is the anchor, and it is the whole point (RNF-27): in plain text,
// changing "a.txt" to "b.txt" is byte-for-byte identical to deleting one line and
// adding another, so only a stable id can say "the same object, new name".
//
// The first column is the row's fate: `y` rename, `n` skip, `d` delete. It is a
// letter rather than a symbol so both states are visible and survive editors that
// trim leading whitespace; the id comes second because identity must not depend on
// position — inserting or removing a line never renumbers the others.
//
// A line without an id is therefore a *create*, which release one refuses
// (RN-FR-19). An unparseable line is an error on that line only — the rest of the
// buffer still parses, because one typo must not hide the other 40 rows.
#include "core/rename_core.h"

#include <cwctype>
#include <string>

namespace rename_core {

namespace {

// A named helper instead of brace-initialising OilError directly: std::wstring's
// constructor from wstring_view is explicit, so an aggregate init carrying a view
// does not compile.
OilError MakeError(std::size_t line, OilErrorKind kind, std::wstring_view text)
{
    OilError error;
    error.line = line;
    error.kind = kind;
    error.text.assign(text);
    return error;
}

bool IsAsciiLetter(wchar_t ch)
{
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

// "<letter> <id> <name>": the state letter is case-insensitive, the separator is
// any run of whitespace, and the name is everything after the id.
std::optional<OilState> StateFromLetter(wchar_t ch)
{
    switch (ch)
    {
    case L'y': case L'Y': return OilState::Rename;
    case L'n': case L'N': return OilState::Skip;
    case L'd': case L'D': return OilState::Delete;
    default: return std::nullopt;
    }
}

} // namespace

OilParseResult ParseOilBuffer(std::wstring_view buffer,
                              const std::vector<OilEntry>& idTable)
{
    OilParseResult out;

    std::size_t lineNumber = 0;
    std::size_t pos = 0;
    while (pos <= buffer.size())
    {
        const std::size_t newline = buffer.find(L'\n', pos);
        const std::size_t end = (newline == std::wstring_view::npos) ? buffer.size() : newline;
        std::wstring_view line = buffer.substr(pos, end - pos);
        ++lineNumber;

        // Tolerate CRLF input: the buffer is user-editable and may be pasted.
        if (!line.empty() && line.back() == L'\r')
            line.remove_suffix(1);

        // Blank lines carry no entry and are not an error; they are how a user
        // separates groups visually.
        bool blank = true;
        for (wchar_t ch : line)
        {
            if (std::iswspace(static_cast<wint_t>(ch)) == 0)
            {
                blank = false;
                break;
            }
        }
        if (blank)
        {
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        // The state letter must be the very first character: leading whitespace
        // is no longer "visual indentation", because it would be indistinguishable
        // from a state column.
        const std::optional<OilState> state = StateFromLetter(line.front());
        if (!state)
        {
            out.errors.push_back(MakeError(lineNumber,
                                           IsAsciiLetter(line.front())
                                               ? OilErrorKind::UnknownState
                                               : OilErrorKind::NoAnchor,
                                           line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        std::size_t cursor = 1;
        while (cursor < line.size() && std::iswspace(static_cast<wint_t>(line[cursor])) != 0)
            ++cursor;

        const std::size_t digitsStart = cursor;
        while (cursor < line.size() && std::iswdigit(static_cast<wint_t>(line[cursor])) != 0)
            ++cursor;

        // No digits at all, or digits glued to the name ("y 1a.txt"): the id is
        // malformed either way, and guessing would attach the rename to the wrong
        // file.
        if (cursor == digitsStart
            || (cursor < line.size() && std::iswspace(static_cast<wint_t>(line[cursor])) == 0))
        {
            out.errors.push_back(MakeError(lineNumber, OilErrorKind::MalformedAnchor, line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        long long id = 0;
        try
        {
            id = std::stoll(std::wstring(line.substr(digitsStart, cursor - digitsStart)));
        }
        catch (const std::exception&)
        {
            // std::stoll throws on overflow; an id that large is never real.
            out.errors.push_back(MakeError(lineNumber, OilErrorKind::MalformedAnchor, line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        while (cursor < line.size() && std::iswspace(static_cast<wint_t>(line[cursor])) != 0)
            ++cursor;

        const std::wstring_view leaf = line.substr(cursor);

        // Order matters here, and getting it wrong is silent:
        //   1. The link form is checked first, because its target contains a
        //      separator ("notes -> /target") and would otherwise be reported as
        //      a path-separator error instead of an unsupported link.
        //   2. Then the interior separator, because a rename must never move the
        //      item. There is no directory marker: a rename never changes the
        //      type, so a folder row is written exactly like a file row.
        if (leaf.find(L" -> ") != std::wstring_view::npos)
        {
            out.errors.push_back(MakeError(lineNumber, OilErrorKind::LinkUnsupported, line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        if (leaf.find(L'/') != std::wstring_view::npos || leaf.find(L'\\') != std::wstring_view::npos)
        {
            out.errors.push_back(MakeError(lineNumber, OilErrorKind::PathSeparator, line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        if (leaf.empty())
        {
            out.errors.push_back(MakeError(lineNumber, OilErrorKind::EmptyName, line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        OilLine parsed;
        parsed.line = lineNumber;
        parsed.id = id;
        parsed.leaf.assign(leaf);
        parsed.state = *state;
        out.lines.push_back(std::move(parsed));

        if (newline == std::wstring_view::npos)
            break;
        pos = newline + 1;
    }

    (void)idTable;   // the differ cross-checks ids; parsing stays purely textual
    return out;
}

} // namespace rename_core
