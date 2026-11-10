// oil_parser.cpp — parse the text buffer into per-line records (design §3.8).
//
// The anchor is the whole point (RNF-27). In plain text, changing "a.txt" to
// "b.txt" is byte-for-byte identical to deleting one line and adding another;
// the "/<id> " prefix is the only way to say "the same object, new name".
//
// A line without an anchor is therefore a *create*, which release one refuses
// (RN-FR-19). An unparseable anchor is an error on that line only — the rest of
// the buffer still parses, because one typo must not hide the other 40 rows.
#include "core/rename_core.h"

#include <cwctype>

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

        OilLine parsed;
        parsed.line = lineNumber;

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

        // Skip visual indentation: it is presentation only (RNF-36).
        std::size_t cursor = 0;
        while (cursor < line.size() && std::iswspace(static_cast<wint_t>(line[cursor])) != 0)
            ++cursor;

        if (cursor >= line.size() || line[cursor] != L'/')
        {
            out.errors.push_back(MakeError(lineNumber, OilErrorKind::NoAnchor, line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        // "/<digits><space><leaf>"
        ++cursor;
        const std::size_t digitsStart = cursor;
        while (cursor < line.size() && std::iswdigit(static_cast<wint_t>(line[cursor])) != 0)
            ++cursor;

        if (cursor == digitsStart)
        {
            out.errors.push_back(MakeError(lineNumber, OilErrorKind::MalformedAnchor, line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        const long long id = std::stoll(std::wstring(line.substr(digitsStart, cursor - digitsStart)));
        if (cursor < line.size() && std::iswspace(static_cast<wint_t>(line[cursor])) != 0)
            ++cursor;

        std::wstring_view leaf = line.substr(cursor);

        // Order matters here, and getting it wrong is silent:
        //   1. The link form is checked first, because its target contains a
        //      separator ("notes -> /target") and would otherwise be reported as
        //      a path-separator error instead of an unsupported link.
        //   2. The directory marker is a *trailing* separator, so it must be
        //      stripped before looking for separators inside the name.
        //   3. Only then is an interior separator a real "this would move the
        //      item" error.
        if (leaf.find(L" -> ") != std::wstring_view::npos)
        {
            out.errors.push_back(MakeError(lineNumber, OilErrorKind::LinkUnsupported, line));
            if (newline == std::wstring_view::npos)
                break;
            pos = newline + 1;
            continue;
        }

        bool isFolder = false;
        if (!leaf.empty() && (leaf.back() == L'/' || leaf.back() == L'\\'))
        {
            isFolder = true;
            leaf.remove_suffix(1);
        }

        // A rename must never move the item, so an interior separator is refused.
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

        parsed.id = id;
        parsed.leaf.assign(leaf);
        parsed.isFolder = isFolder;
        out.lines.push_back(std::move(parsed));

        if (newline == std::wstring_view::npos)
            break;
        pos = newline + 1;
    }

    (void)idTable;   // the differ cross-checks ids; parsing stays purely textual
    return out;
}

} // namespace rename_core
