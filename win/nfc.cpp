// nfc.cpp — install the OS Unicode normalizer into the pure core (design §3.6).
//
// The core cannot call NormalizeString: <windows.h> is banned there so the L1
// tests stay portable. It exposes SetNfcNormalizer instead, and this is the one
// place that fills it in. If normalization fails the input is returned unchanged,
// which makes the comparison fall back to raw code units rather than guessing.
#include "win/platform.h"

#include <windows.h>

namespace rename_win {

namespace {

std::wstring NormalizeC(std::wstring_view text)
{
    if (text.empty())
        return std::wstring();

    const int needed = ::NormalizeString(NormalizationC, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0)
        return std::wstring(text);

    // `needed` already accounts for the terminator when the source length is
    // given explicitly and positive? It does not; it is the character count the
    // destination needs, so allocate exactly that plus one for safety.
    std::wstring buffer(static_cast<std::size_t>(needed) + 1, L'\0');
    const int written = ::NormalizeString(NormalizationC, text.data(),
                                          static_cast<int>(text.size()),
                                          buffer.data(),
                                          static_cast<int>(buffer.size()));
    if (written <= 0)
        return std::wstring(text);

    buffer.resize(static_cast<std::size_t>(written));
    return buffer;
}

} // namespace

void InstallNfcNormalizer()
{
    rename_core::SetNfcNormalizer(NormalizeC);
}

} // namespace rename_win
