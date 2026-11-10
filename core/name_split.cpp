// name_split.cpp — scope splitting and re-joining (design §3.5, RNF-17).
//
// The rule that matters: `extension` is the final nonempty suffix without its
// dot, and only when a name precedes that dot. So "archive.tar.gz" is
// stem "archive.tar" + extension "gz", while ".gitignore" and "README" have no
// extension at all. Getting this wrong is what makes extension-only rules
// silently eat dotfiles.
#include "core/rename_core.h"

namespace rename_core {

SplitName Split(std::wstring_view leaf)
{
    SplitName out;

    // The dot must not be the first character: ".gitignore" is a name, not an
    // extension. A trailing dot is not a suffix either (Windows refuses the
    // name outright, and trimming happens later in Finalize).
    const std::size_t dot = leaf.find_last_of(L'.');
    if (dot == std::wstring_view::npos || dot == 0 || dot + 1 >= leaf.size())
    {
        out.stem.assign(leaf);
        return out;
    }

    out.stem.assign(leaf.substr(0, dot));
    out.extension.assign(leaf.substr(dot + 1));
    out.hasExtension = true;
    return out;
}

std::wstring Join(std::wstring_view stem, std::wstring_view extension, bool hasExtension)
{
    std::wstring out(stem);
    if (hasExtension && !extension.empty())
    {
        out.push_back(L'.');
        out.append(extension);
    }
    return out;
}

ScopePortion Portion(std::wstring_view leaf, Scope scope)
{
    ScopePortion out;

    if (scope == Scope::Full)
    {
        out.text.assign(leaf);
        return out;
    }

    const SplitName parts = Split(leaf);
    if (scope == Scope::Name)
    {
        out.text = parts.stem;
        return out;
    }

    // Extension scope. An extensionless leaf has nothing to operate on: report
    // it so the caller can display "no extension; extension rule not applied"
    // instead of letting a regex that matches an empty string invent a suffix.
    if (!parts.hasExtension)
    {
        out.applies = false;
        return out;
    }
    out.text = parts.extension;
    return out;
}

} // namespace rename_core
