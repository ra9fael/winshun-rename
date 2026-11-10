// counter.cpp — batch-scoped counters (design §3.7, RNF-16).
//
// Two behaviours here are the ones users notice immediately when they are wrong:
// the index continues across items instead of restarting per file, and an item
// that did not match does not consume a number. PowerRename does both (RNF-16);
// we keep them because they match the mental model of "number the files I am
// renaming", not "number every file I selected".
#include "core/rename_core.h"

#include <algorithm>
#include <array>
#include <cwchar>

namespace rename_core {

std::wstring FormatCounter(const CounterState& counter)
{
    // padding is clamped well below any legal name length; a huge value would
    // otherwise ask swprintf for a gigantic buffer long before the length check
    // in ExceedsLength could refuse it.
    const unsigned padding = counter.padding % 260u;

    // `value` already holds the number to print (it starts at `start`), so no
    // off-by-one conversion happens here.
    const long long shown = counter.value;

    std::array<wchar_t, 64> buffer{};
    std::array<wchar_t, 16> format{};

    // "%0<width>lld", and plain "%lld" when no padding is asked for. Building
    // this format is the one place where the length modifier is easy to lose:
    // "%%ll" alone is not a conversion, so swprintf fails and every counter
    // comes out empty. The tests caught exactly that.
    if (padding > 0)
        std::swprintf(format.data(), format.size(), L"%%0%ull%lc", padding, L'd');
    else
        std::swprintf(format.data(), format.size(), L"%%lld");

    // If the number plus padding does not fit, fall back to unpadded output
    // rather than truncating digits — a truncated counter is a wrong name.
    if (std::swprintf(buffer.data(), buffer.size(), format.data(), shown) <= 0)
        std::swprintf(buffer.data(), buffer.size(), L"%lld", shown);

    return std::wstring(buffer.data());
}

} // namespace rename_core
