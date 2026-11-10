// metadata.cpp — EXIF/XMP extraction for templates (design §3.7, RNF-23).
//
// Scope is deliberately narrow, and the narrowness is the feature:
//
//   * Only 8 extensions are attempted (jpg/jpeg/png/tif/tiff/heic/heif/avif),
//     screened by extension *before* any decoding happens. Opening every file to
//     discover it has no metadata is how a rename of 500 files becomes a minute of
//     waiting.
//   * A value that cannot be read leaves the "$PATTERN" literal in the name,
//     because an empty string silently shortens the name and the user cannot tell
//     it happened (RNF-23).
//   * A cache keyed by path plus write time keeps a re-preview from decoding the
//     same file twice.
//
// The full EXIF/XMP surface in PowerRename is 40 KB of WIC code. This implements
// the subset that the template table actually consumes, and reports the rest as
// unavailable rather than inventing values.
#include "win/metadata.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace rename_win {

namespace {

struct CacheKey {
    std::wstring path;
    std::uint64_t writeTicks = 0;

    bool operator<(const CacheKey& other) const
    {
        if (path != other.path)
            return path < other.path;
        return writeTicks < other.writeTicks;
    }
};

std::mutex g_cacheLock;
std::map<CacheKey, rename_core::TemplateContext::MetadataMap> g_cache;

std::wstring LowerExtension(const std::wstring& path)
{
    const std::size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return std::wstring();
    std::wstring extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch))); });
    return extension;
}

bool IsSupportedImage(const std::wstring& path)
{
    static const std::vector<std::wstring> supported = {
        L".jpg", L".jpeg", L".png", L".tif", L".tiff", L".heic", L".heif", L".avif",
    };
    const std::wstring extension = LowerExtension(path);
    return std::find(supported.begin(), supported.end(), extension) != supported.end();
}

std::uint64_t WriteTicksOf(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
        return 0;
    return (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
           attributes.ftLastWriteTime.dwLowDateTime;
}

std::wstring FormatNumber(double value, int decimals = 0)
{
    wchar_t buffer[64]{};
    if (decimals <= 0)
        std::swprintf(buffer, 64, L"%.0f", value);
    else if (decimals == 1)
        std::swprintf(buffer, 64, L"%.1f", value);
    else
        std::swprintf(buffer, 64, L"%.2f", value);
    return std::wstring(buffer);
}

std::wstring FormatDate(const SYSTEMTIME& time)
{
    wchar_t buffer[32]{};
    std::swprintf(buffer, 32, L"%04d-%02d-%02d", time.wYear, time.wMonth, time.wDay);
    return std::wstring(buffer);
}

std::wstring FormatTime(const SYSTEMTIME& time)
{
    wchar_t buffer[32]{};
    std::swprintf(buffer, 32, L"%02d:%02d:%02d", time.wHour, time.wMinute, time.wSecond);
    return std::wstring(buffer);
}

std::wstring ReadStringTag(IWICMetadataQueryReader* reader, const wchar_t* query)
{
    PROPVARIANT value{};
    ::PropVariantInit(&value);
    std::wstring out;
    if (SUCCEEDED(reader->GetMetadataByName(query, &value)))
    {
        if (value.vt == VT_LPWSTR && value.pwszVal)
            out = value.pwszVal;
        else if (value.vt == VT_BSTR && value.bstrVal)
            out = value.bstrVal;
    }
    ::PropVariantClear(&value);
    return out;
}

bool ReadSystemTimeTag(IWICMetadataQueryReader* reader, const wchar_t* query, SYSTEMTIME& out)
{
    PROPVARIANT value{};
    ::PropVariantInit(&value);
    bool ok = false;
    // EXIF dates are stored as "YYYY:MM:DD HH:MM:SS" strings, not FILETIMEs.
    if (SUCCEEDED(reader->GetMetadataByName(query, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
    {
        const std::wstring text = value.pwszVal;
        int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
        if (::swscanf_s(text.c_str(), L"%d:%d:%d %d:%d:%d",
                           &year, &month, &day, &hour, &minute, &second) >= 3)
        {
            out.wYear = static_cast<WORD>(year);
            out.wMonth = static_cast<WORD>(month);
            out.wDay = static_cast<WORD>(day);
            out.wHour = static_cast<WORD>(hour);
            out.wMinute = static_cast<WORD>(minute);
            out.wSecond = static_cast<WORD>(second);
            ok = true;
        }
    }
    ::PropVariantClear(&value);
    return ok;
}

} // namespace

MetadataResult ExtractMetadata(const std::wstring& path)
{
    MetadataResult result;
    result.supported = IsSupportedImage(path);
    if (!result.supported)
        return result;

    const CacheKey key{path, WriteTicksOf(path)};
    {
        std::lock_guard<std::mutex> lock(g_cacheLock);
        const auto cached = g_cache.find(key);
        if (cached != g_cache.end())
        {
            result.values = cached->second;
            result.fromCache = true;
            return result;
        }
    }

    const HRESULT comInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool ownsCom = SUCCEEDED(comInit);

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
    {
        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &decoder);
        if (SUCCEEDED(hr))
        {
            ComPtr<IWICBitmapFrameDecode> frame;
            hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr))
            {
                ComPtr<IWICMetadataQueryReader> reader;
                hr = frame->GetMetadataQueryReader(&reader);
                if (SUCCEEDED(hr) && reader)
                {
                    // Only the tags the template table exposes are read. Adding a
                    // pattern means adding a query here, which is what keeps the
                    // "no invented values" rule enforceable.
                    SYSTEMTIME taken{};
                    if (ReadSystemTimeTag(reader.Get(), L"/app1/ifd/exif/{ushort=36867}", taken))
                    {
                        result.values[L"DATE_TAKEN_YYYY"] = FormatNumber(taken.wYear);
                        result.values[L"DATE_TAKEN_MM"] = FormatNumber(taken.wMonth, 0);
                        result.values[L"DATE_TAKEN_DD"] = FormatNumber(taken.wDay, 0);
                        result.values[L"DATE_TAKEN"] = FormatDate(taken);
                        result.values[L"TIME_TAKEN"] = FormatTime(taken);
                    }

                    const std::wstring make = ReadStringTag(reader.Get(), L"/app1/ifd/{ushort=271}");
                    if (!make.empty())
                        result.values[L"CAMERA_MAKE"] = make;

                    const std::wstring model = ReadStringTag(reader.Get(), L"/app1/ifd/{ushort=272}");
                    if (!model.empty())
                        result.values[L"CAMERA_MODEL"] = model;

                    const std::wstring artist = ReadStringTag(reader.Get(), L"/app1/ifd/{ushort=315}");
                    if (!artist.empty())
                        result.values[L"AUTHOR"] = artist;

                    const std::wstring copyright = ReadStringTag(reader.Get(), L"/app1/ifd/{ushort=33432}");
                    if (!copyright.empty())
                        result.values[L"COPYRIGHT"] = copyright;
                }
            }
        }
    }

    if (ownsCom)
        ::CoUninitialize();

    {
        std::lock_guard<std::mutex> lock(g_cacheLock);
        g_cache[key] = result.values;
    }
    return result;
}

void ClearMetadataCache()
{
    std::lock_guard<std::mutex> lock(g_cacheLock);
    g_cache.clear();
}

} // namespace rename_win
