// finalize.cpp — the three fixed steps: trim, transform, compare (design §3.6).
//
// Order is not negotiable. Compare-before-transform would report a case-only
// rename as "unchanged"; transform-before-trim would uppercase the trailing
// spaces and dots that are about to be removed, producing a name the user never
// asked for.
#include "core/rename_core.h"

#include <cwctype>
#include <utility>

namespace rename_core {

namespace {

NfcNormalizer& installedNormalizer()
{
    static NfcNormalizer instance;   // empty = identity
    return instance;
}

// Non-breaking space is the one character we rewrite rather than reject. Pasting
// a name out of a web page or a PDF routinely carries U+00A0, and a file named
// with one is legal but nearly impossible to type again. PowerRename rewrites it
// too (RNF-19), and we surface it via FinalizeResult::hadNbsp so the UI can say
// so instead of doing it silently.
void RewriteNbsp(std::wstring& text, bool& hadNbsp)
{
    for (wchar_t& ch : text)
    {
        if (ch == L'\u00A0')
        {
            ch = L' ';
            hadNbsp = true;
        }
    }
}

bool IsTrimmable(wchar_t ch)
{
    return std::iswspace(static_cast<wint_t>(ch)) != 0 || ch == L'.';
}

} // namespace

void SetNfcNormalizer(NfcNormalizer normalizer) { installedNormalizer() = std::move(normalizer); }

std::wstring NfcNormalize(std::wstring_view text)
{
    if (installedNormalizer())
        return installedNormalizer()(text);
    return std::wstring(text);
}

bool NfcEqual(std::wstring_view a, std::wstring_view b)
{
    if (a.size() == b.size() && a == b)
        return true;   // fast path: identical code units are certainly equal
    return NfcNormalize(a) == NfcNormalize(b);
}

bool HasInvalidCharacter(std::wstring_view leaf)
{
    // The Windows set, RNF-20. Note that '\' and '/' are included because a
    // rename must not move the item; U+00A0 is not in the list (we rewrite it).
    for (wchar_t ch : leaf)
    {
        switch (ch)
        {
        case L'<': case L'>': case L':': case L'"':
        case L'\\': case L'/': case L'|': case L'?': case L'*':
            return true;
        default:
            break;
        }
    }
    return false;
}

bool ExceedsLength(std::size_t pathLength,
                   std::wstring_view originalLeaf,
                   std::wstring_view targetLeaf,
                   bool isFolder)
{
    // Windows measures the full path in characters. Apply the change as a delta
    // so a caller does not have to rebuild the path just to ask the question.
    // Directories get the tighter budget (247) because a child name still has to
    // fit underneath.
    const std::size_t limit = isFolder ? 247u : 260u;
    const long long delta = static_cast<long long>(targetLeaf.size())
                          - static_cast<long long>(originalLeaf.size());
    const long long projected = static_cast<long long>(pathLength) + delta;
    return projected > static_cast<long long>(limit);
}

FinalizeResult Finalize(std::wstring_view candidate,
                        std::wstring_view originalLeaf,
                        std::uint32_t flags,
                        bool isFolder)
{
    (void)isFolder;   // directories follow the same three steps
    FinalizeResult out;

    std::wstring text(candidate);
    RewriteNbsp(text, out.hadNbsp);

    // Step 1: trim. Leading whitespace and trailing whitespace-or-dot. A name
    // that is entirely spaces and dots trims to nothing, which is reported as an
    // empty name rather than passed on to be refused later.
    std::size_t begin = 0;
    while (begin < text.size() && std::iswspace(static_cast<wint_t>(text[begin])) != 0)
        ++begin;

    std::size_t end = text.size();
    while (end > begin && IsTrimmable(text[end - 1]))
        --end;

    if (begin != 0 || end != text.size())
    {
        out.trimmed = true;
        text = text.substr(begin, end - begin);
    }

    if (text.empty())
    {
        out.text.clear();
        out.status = Status::ErrorEmptyName;
        return out;
    }

    // Step 2: transform. Case work happens on the trimmed text so the user sees
    // the transformation applied to what will actually be written.
    text = ApplyCase(text, flags);

    if (text.empty())
    {
        // A transform cannot empty a non-empty string today, but if a future one
        // can (a mapping that strips), do not let it through.
        out.text.clear();
        out.status = Status::ErrorEmptyName;
        return out;
    }

    if (HasInvalidCharacter(text))
    {
        out.text = std::move(text);
        out.status = Status::ErrorInvalidCharacter;
        return out;
    }

    // Step 3: compare. NFC equality, so an NFD name on disk and its NFC form are
    // "the same name" and therefore unchanged.
    out.text = std::move(text);
    out.status = NfcEqual(out.text, originalLeaf) ? Status::Unchanged : Status::Ready;
    return out;
}

} // namespace rename_core
