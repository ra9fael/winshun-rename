// platform.h — the Windows-facing surface used by rename.exe (M2, design §3.1).
//
// Everything in win/ may include <windows.h>; nothing in core/ may. That split
// is enforced by tools/check_core_purity.ps1 as a ctest, so it cannot rot.
//
// The interface is deliberately narrow: enumerate, evaluate, execute. The UI
// talks to a plan, never to a handle, which is what keeps the oil mode and the
// control mode on one code path.
#pragma once

#include "core/rename_core.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rename_win {

// Install NormalizeString(NormalizationC) as the core's NFC normalizer. Called
// once at startup; without it the core compares raw code units, which is the
// honest fallback and is what the L1 tests exercise.
void InstallNfcNormalizer();

// One selected path, as the host handed it over.
struct CollectedItem {
    std::string id;              // decimal, stable for the lifetime of the batch
    std::wstring fullPath;
    std::wstring leaf;
    bool isFolder = false;
    unsigned depth = 0;          // 0 for a directly selected item
    std::size_t pathLength = 0;
};

struct EnumerateRequest {
    std::vector<std::wstring> roots;   // from the host's `files` array
    bool recursive = false;            // RN-FR-14, off by default
    bool followLinks = false;          // always false in release one
};

struct EnumerateResult {
    std::vector<CollectedItem> items;
    std::vector<std::wstring> notes;   // de-duplication and skipped-entry notes
    bool blocked = false;              // an incomplete set was detected
    std::wstring blockReason;
};

EnumerateResult Enumerate(const EnumerateRequest& request);

// Fill a TemplateContext's date, month/weekday names and metadata for one item.
// The names come from GetDateFormatEx in the user locale, which is why this
// lives here and not in the core.
rename_core::TemplateContext BuildTemplateContext(const CollectedItem& item,
                                                  std::uint32_t flags);

// ---- execution ---------------------------------------------------------------

struct ExecuteItem {
    std::wstring fullPath;       // original full path
    std::wstring targetLeaf;     // new leaf name
    bool isFolder = false;
    unsigned depth = 0;
};

enum class ExecuteOutcome {
    Renamed,
    Skipped,
    IdentityChanged,     // size/time/volume changed since the plan was built
    Refused,             // target exists, invalid name, or other blocking reason
    Failed,
};

struct ExecuteResultItem {
    std::wstring originalPath;
    std::wstring targetPath;
    ExecuteOutcome outcome = ExecuteOutcome::Failed;
    std::wstring detail;         // human-readable reason for anything not Renamed
};

struct ExecuteResult {
    std::vector<ExecuteResultItem> items;
    bool undoAvailable = false;          // Shell undo stack accepted the batch
    std::wstring batchJournalPath;       // read-only diagnostic record
};

// Submit one batch through IFileOperation, deepest-first, with no
// RENAMEONCOLLISION so an existing target can never be silently bumped to
// "name (1)" (RN-FR-05, RN-T6-01).
ExecuteResult Execute(const std::vector<ExecuteItem>& items, const std::wstring& dataDir);

} // namespace rename_win
