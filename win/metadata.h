// metadata.h — EXIF/XMP extraction (design §3.7, RNF-23).
//
// Separate from platform.h so the WIC dependency stays isolated: a build that
// never needs metadata can drop this translation unit and link cleanly.
#pragma once

#include "core/rename_core.h"

#include <string>

namespace rename_win {

struct MetadataResult {
    // False means the extension is outside the supported set, so the caller should
    // not even ask again. Distinct from "supported but nothing found", which is
    // the common case for a PNG screenshot.
    bool supported = false;
    bool fromCache = false;
    rename_core::TemplateContext::MetadataMap values;
};

// Read the supported tags from one file. Returns an empty map (not an error) when
// the file is a supported type without those tags.
MetadataResult ExtractMetadata(const std::wstring& path);

// Drop the cache. Call when a batch finishes so a later rename of the same file
// re-reads it.
void ClearMetadataCache();

} // namespace rename_win
