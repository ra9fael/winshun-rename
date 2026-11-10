// template_context.cpp — fill dates and metadata from the OS (design §3.7).
//
// Month and weekday names come from GetDateFormatEx in the user's locale, so
// "$MMMM" reads "March" on an English machine and "三月" on a Chinese one. That
// call lives here because it needs <windows.h>; the core only receives strings.
//
// Metadata extraction (EXIF/XMP through WIC) is the last item in M5. Until it
// lands, `metadata` stays empty, and a "$DATE_TAKEN_YYYY" template keeps its
// literal text — the documented behaviour when a value is unavailable (RNF-23),
// not a silent empty string.
#include "win/platform.h"
#include "win/metadata.h"

#include <windows.h>

#include <cwctype>
#include <string>

namespace rename_win {

namespace {

std::wstring FormatInLocale(const SYSTEMTIME& time, const wchar_t* format)
{
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
    if (::GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) == 0)
        wcscpy_s(localeName, L"en-US");

    wchar_t buffer[128]{};
    const int written = ::GetDateFormatEx(localeName, 0, &time, format, buffer,
                                          static_cast<int>(std::size(buffer)), nullptr);
    if (written <= 0)
        return std::wstring();

    // The first letter is capitalised: filenames conventionally start a word with
    // an uppercase letter, and PowerRename does the same (RNF-21).
    std::wstring out(buffer, static_cast<std::size_t>(written) - 1);
    if (!out.empty())
        out[0] = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(out[0])));
    return out;
}

FILETIME ReadTimestamp(const std::wstring& path, std::uint32_t flags)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return FILETIME{};

    // The flag picks which timestamp the templates see; modification time is the
    // default because that is what users mean by "the file's date".
    if ((flags & rename_core::FlagCreatedTime) != 0)
        return data.ftCreationTime;
    if ((flags & rename_core::FlagAccessedTime) != 0)
        return data.ftLastAccessTime;
    return data.ftLastWriteTime;
}

} // namespace

rename_core::TemplateContext BuildTemplateContext(const CollectedItem& item,
                                                  std::uint32_t flags)
{
    rename_core::TemplateContext context;

    const FILETIME fileTime = ReadTimestamp(item.fullPath, flags);
    if (fileTime.dwLowDateTime != 0 || fileTime.dwHighDateTime != 0)
    {
        FILETIME utc{};
        SYSTEMTIME utcSystem{};
        SYSTEMTIME localSystem{};
        if (::FileTimeToLocalFileTime(&fileTime, &utc) &&
            ::FileTimeToSystemTime(&utc, &utcSystem))
        {
            // FileTimeToLocalFileTime already produced local time represented as
            // a FILETIME; converting it again through SystemTime keeps the fields
            // consistent for the formatters.
            localSystem = utcSystem;

            context.date.year = localSystem.wYear;
            context.date.month = localSystem.wMonth;
            context.date.day = localSystem.wDay;
            context.date.hour = localSystem.wHour;
            context.date.minute = localSystem.wMinute;
            context.date.second = localSystem.wSecond;
            context.date.millisecond = localSystem.wMilliseconds;

            context.monthLong = FormatInLocale(localSystem, L"MMMM");
            context.monthShort = FormatInLocale(localSystem, L"MMM");
            context.weekdayLong = FormatInLocale(localSystem, L"dddd");
            context.weekdayShort = FormatInLocale(localSystem, L"ddd");
        }
    }

    // Metadata is only attempted when the flags ask for it, so a batch of plain
    // text files never pays for a WIC open (RNF-23).
    const bool wantsMetadata = (flags & (rename_core::FlagMetadataExif | rename_core::FlagMetadataXmp)) != 0;
    if (wantsMetadata && !item.isFolder)
    {
        const MetadataResult metadata = ExtractMetadata(item.fullPath);
        if (metadata.supported)
            context.metadata = metadata.values;
    }

    return context;
}

} // namespace rename_win
