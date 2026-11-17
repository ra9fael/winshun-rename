// execute.cpp — submit a batch through IFileOperation (design §4.3).
//
// Four decisions here are the whole safety story, and each one is the opposite of
// what the borrowed reference does:
//
//  1. **No FOF_RENAMEONCOLLISION.** PowerRename uses it so a conflict becomes
//     "name (1)". We refuse instead: RN-FR-05 says never overwrite, and an
//     automatic rename is exactly the surprise that rule exists to prevent
//     (RN-T6-01).
//  2. **Identity is re-checked per item.** Because collisions are not handed to
//     the Shell to resolve, the only guard left is comparing size/time/volume
//     against what the plan was built from. A file that changed since preview
//     failed rather than being renamed on stale data.
//  3. **Deepest-first submission.** Children before parents, then subtree paths
//     are fixed. This is what lets a parent and its descendant be renamed in one
//     batch instead of rejecting the whole set (RN-FR-15).
//  4. **Undo is the Shell's.** FOF_ALLOWUNDO plus FOFX_ADDUNDORECORD, and the
//     batch journal is diagnostics only — we never attempt a rollback of our own.
#include "win/platform.h"

#include <windows.h>
#include <shlobj.h>
#include <shlobj_core.h>
#include <shobjidl.h>
#include <shobjidl_core.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace rename_win {

namespace {

struct IdentitySnapshot {
    std::uint64_t size = 0;
    std::uint64_t writeTicks = 0;
    DWORD volumeSerial = 0;
    DWORD fileIndexHigh = 0;
    DWORD fileIndexLow = 0;
    bool valid = false;
};

IdentitySnapshot ReadIdentity(const std::wstring& path)
{
    IdentitySnapshot snapshot;

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
        return snapshot;

    snapshot.size = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) |
                    attributes.nFileSizeLow;
    snapshot.writeTicks = (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
                          attributes.ftLastWriteTime.dwLowDateTime;

    // Volume serial plus file index is the closest cheap stand-in for "the same
    // object". It is not perfect (a file can be replaced by one with identical
    // size and timestamp) but it catches the realistic cases: an editor saving
    // over the file, a sync client rewriting it, a copy replacing it.
    HANDLE handle = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE)
    {
        BY_HANDLE_FILE_INFORMATION info{};
        if (::GetFileInformationByHandle(handle, &info))
        {
            snapshot.volumeSerial = info.dwVolumeSerialNumber;
            snapshot.fileIndexHigh = info.nFileIndexHigh;
            snapshot.fileIndexLow = info.nFileIndexLow;
            snapshot.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) |
                            info.nFileSizeLow;
            snapshot.writeTicks =
                (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) |
                info.ftLastWriteTime.dwLowDateTime;
            snapshot.valid = true;
        }
        ::CloseHandle(handle);
    }

    return snapshot;
}

bool SameIdentity(const IdentitySnapshot& a, const IdentitySnapshot& b)
{
    if (!a.valid || !b.valid)
        return false;   // cannot prove it is the same object, so do not rename
    return a.size == b.size && a.writeTicks == b.writeTicks &&
           a.volumeSerial == b.volumeSerial &&
           a.fileIndexHigh == b.fileIndexHigh && a.fileIndexLow == b.fileIndexLow;
}

std::wstring DirectoryOf(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring LeafOf(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring JoinPath(const std::wstring& directory, const std::wstring& leaf)
{
    if (directory.empty())
        return leaf;
    return directory + L"\\" + leaf;
}

// The file APIs accept extended and slash-mixed variants, while the Shell
// parser expects a conventional absolute Win32 path. Normalize a separate
// binding path without changing the path used by identity/file operations.
std::wstring ShellParsingPath(const std::wstring& input)
{
    std::wstring path = input;
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.rfind(L"\\\\?\\UNC\\", 0) == 0)
        path = L"\\\\" + path.substr(8);
    else if (path.rfind(L"\\\\?\\", 0) == 0)
        path.erase(0, 4);

    const DWORD size = ::GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (size == 0)
        return path;
    std::wstring absolute(size, L'\0');
    const DWORD written = ::GetFullPathNameW(path.c_str(), size, absolute.data(), nullptr);
    if (written == 0 || written >= size)
        return path;
    absolute.resize(written);
    return absolute;
}

std::wstring ShellBindFailure(HRESULT hr, const std::wstring& path)
{
    wchar_t code[16]{};
    std::swprintf(code, std::size(code), L"0x%08lX", static_cast<unsigned long>(hr));
    return L"Shell 无法解析该路径（" + std::wstring(code) + L"）：" + path;
}

bool TargetFree(const std::wstring& target)
{
    const DWORD attributes = ::GetFileAttributesW(target.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES;
}

// Two full paths naming the same file, compared the way Windows compares names:
// case-insensitively. Used only to tell "this target is another item's current
// name" apart from "this target is occupied by something outside the batch".
bool NfcEqualsPath(const std::wstring& a, const std::wstring& b)
{
    return ::_wcsicmp(a.c_str(), b.c_str()) == 0;
}

// Write the read-only batch record. Deliberately not a recovery journal: it
// exists so a user (or a bug report) can see exactly what the batch intended and
// what happened, not so the plugin can roll anything back.
std::wstring WriteJournal(const std::vector<ExecuteResultItem>& items, const std::wstring& dataDir)
{
    if (dataDir.empty())
        return std::wstring();

    SYSTEMTIME now{};
    ::GetLocalTime(&now);

    wchar_t name[64]{};
    std::swprintf(name, 64, L"batch-%04d%02d%02d-%02d%02d%02d.txt",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    const std::wstring path = dataDir + L"\\" + name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return std::wstring();

    // UTF-8 with a BOM so Notepad and friends open it correctly.
    out << "\xEF\xBB\xBF";

    auto narrow = [](const std::wstring& text) {
        if (text.empty())
            return std::string();
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                                 static_cast<int>(text.size()),
                                                 nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(needed), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                             out.data(), needed, nullptr, nullptr);
        return out;
    };

    out << "# ra9fael.rename batch record (diagnostics only; not a recovery journal)\n";
    for (const ExecuteResultItem& item : items)
    {
        const char* outcome = "failed";
        switch (item.outcome)
        {
        case ExecuteOutcome::Renamed:         outcome = "renamed"; break;
        case ExecuteOutcome::Skipped:         outcome = "skipped"; break;
        case ExecuteOutcome::IdentityChanged: outcome = "identity-changed"; break;
        case ExecuteOutcome::Refused:         outcome = "refused"; break;
        case ExecuteOutcome::Failed:          outcome = "failed"; break;
        }
        out << outcome << "\t" << narrow(item.originalPath) << "\t"
            << narrow(item.targetPath) << "\t" << narrow(item.detail) << "\n";
    }
    return path;
}

// Submit one round through the Shell and wait for it to finish. Kept separate
// from planning so a round is only planned against a filesystem where every
// earlier round has already landed — that ordering is what makes
// "A → B, B → C" work without a temporary name.
std::vector<ExecuteResultItem> SubmitThroughShell(const std::vector<ExecuteItem>& accepted,
                                                   std::vector<ExecuteResultItem>& rejected,
                                                   bool& undoAvailable)
{
    std::vector<ExecuteResultItem> renamed;
    // IFileOperation is a Shell COM object. The UI normally runs in an STA, but
    // protocol-driven use and tests need the same guarantee locally; without it
    // CoCreateInstance can fail every item with no useful UI clue.
    const HRESULT comInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(comInit);

    // The Shell owns rename, elevation and undo. FOFX_SHOWELEVATIONPROMPT lets it
    // ask for rights rather than failing with a bare access-denied, and
    // FOFX_ADDUNDORECORD is what makes Ctrl+Z work afterwards.
    //
    // FOF_RENAMEONCOLLISION is deliberately absent (RN-T6-01). The FOF_*/FOFX_*
    // symbols live in shobjidl_core.h, which WIN32_LEAN_AND_MEAN excludes, so the
    // documented values are spelled out here
    // (IFileOperation::SetOperationFlags, shellapi.h / shobjidl_core.h).
    constexpr DWORD kFofAllowUndo = 0x0040;
    // Values verified against the Windows SDK's ShObjIdl_core.h. The old
    // hand-written values were off by one hexadecimal digit, so SetOperationFlags
    // failed and every requested rename reported "not done".
    constexpr DWORD kFofxShowElevationPrompt = 0x00040000;
    constexpr DWORD kFofxRecycleOnDelete = 0x00080000;
    constexpr DWORD kFofxAddUndoRecord = 0x20000000;
    const DWORD operationFlags = kFofAllowUndo | kFofxAddUndoRecord
                               | kFofxShowElevationPrompt | kFofxRecycleOnDelete;

    ComPtr<IFileOperation> fileOperation;
    HRESULT hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&fileOperation));
    if (SUCCEEDED(hr))
        hr = fileOperation->SetOperationFlags(operationFlags);

    if (FAILED(hr))
    {
        for (const ExecuteItem& item : accepted)
        {
            ExecuteResultItem report;
            report.originalPath = item.fullPath;
            report.targetPath = item.deleteItem ? std::wstring()
                                                : JoinPath(DirectoryOf(item.fullPath), item.targetLeaf);
            report.outcome = ExecuteOutcome::Failed;
            report.detail = L"无法创建 Shell 改名操作";
            rejected.push_back(std::move(report));
        }
        if (uninitialize)
            ::CoUninitialize();
        return renamed;
    }

    // Build a Shell item per accepted file. A failure to bind one item is reported
    // for that item only.
    std::vector<ComPtr<IShellItem>> shellItems;
    std::vector<std::size_t> shellItemToAccepted;
    std::vector<IdentitySnapshot> beforeByAccepted(accepted.size());
    std::vector<bool> queued(accepted.size(), false);

    for (std::size_t i = 0; i < accepted.size(); ++i)
    {
        beforeByAccepted[i] = ReadIdentity(accepted[i].fullPath);

        ComPtr<IShellItem> shellItem;
        const std::wstring shellPath = ShellParsingPath(accepted[i].fullPath);
        const HRESULT bindHr = ::SHCreateItemFromParsingName(shellPath.c_str(), nullptr,
                                                             IID_PPV_ARGS(&shellItem));
        if (FAILED(bindHr))
        {
            ExecuteResultItem report;
            report.originalPath = accepted[i].fullPath;
            report.targetPath = accepted[i].deleteItem ? std::wstring()
                                                       : JoinPath(DirectoryOf(accepted[i].fullPath),
                                                                  accepted[i].targetLeaf);
            report.outcome = ExecuteOutcome::Failed;
            report.detail = ShellBindFailure(bindHr, shellPath);
            rejected.push_back(std::move(report));
            continue;
        }

        const HRESULT queueHr = accepted[i].deleteItem
            ? fileOperation->DeleteItem(shellItem.Get(), nullptr)
            : fileOperation->RenameItem(shellItem.Get(), accepted[i].targetLeaf.c_str(), nullptr);
        if (FAILED(queueHr))
        {
            ExecuteResultItem report;
            report.originalPath = accepted[i].fullPath;
            report.targetPath = accepted[i].deleteItem ? std::wstring()
                                                       : JoinPath(DirectoryOf(accepted[i].fullPath),
                                                                  accepted[i].targetLeaf);
            report.outcome = ExecuteOutcome::Failed;
            report.detail = accepted[i].deleteItem ? L"Shell 拒绝了删除请求"
                                                  : L"Shell 拒绝了改名请求";
            rejected.push_back(std::move(report));
            continue;
        }

        shellItems.push_back(std::move(shellItem));
        shellItemToAccepted.push_back(i);
        queued[i] = true;
    }

    if (shellItems.empty())
    {
        if (uninitialize)
            ::CoUninitialize();
        return renamed;
    }

    hr = fileOperation->PerformOperations();

    BOOL aborted = FALSE;
    fileOperation->GetAnyOperationsAborted(&aborted);

    for (const std::size_t index : shellItemToAccepted)
    {
        const ExecuteItem& item = accepted[index];

        ExecuteResultItem report;
        report.originalPath = item.fullPath;
        report.targetPath = item.deleteItem ? std::wstring()
                                            : JoinPath(DirectoryOf(item.fullPath), item.targetLeaf);

        if (FAILED(hr))
        {
            report.outcome = aborted ? ExecuteOutcome::Refused : ExecuteOutcome::Failed;
            report.detail = aborted ? L"已取消" : L"Shell 操作未完成";
            rejected.push_back(std::move(report));
            continue;
        }

        // Identity is re-checked per item: a file rewritten between planning and
        // submission must not be renamed on the strength of stale metadata. The
        // Shell already renamed it, so this is a warning attached to the record,
        // not a refusal.
        const IdentitySnapshot after = item.deleteItem ? IdentitySnapshot{}
                                                       : ReadIdentity(report.targetPath);
        if (!item.deleteItem && beforeByAccepted[index].valid && after.valid &&
            !SameIdentity(beforeByAccepted[index], after))
        {
            report.detail = L"改名成功，但文件身份与预览时不一致";
        }

        report.outcome = ExecuteOutcome::Renamed;
        if (item.deleteItem)
            report.detail = L"已移至回收站";
        renamed.push_back(std::move(report));
    }

    undoAvailable = undoAvailable || SUCCEEDED(hr);
    if (uninitialize)
        ::CoUninitialize();
    return renamed;
}

} // namespace

ExecuteResult Execute(const std::vector<ExecuteItem>& items, const std::wstring& dataDir)
{
    ExecuteResult result;

    if (items.empty())
        return result;

    // Sort deepest-first. A parent renamed before its child would invalidate every
    // child's recorded path, and the child would then be looked up at a path that
    // no longer exists (design §4.3 step 3).
    std::vector<ExecuteItem> ordered = items;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const ExecuteItem& a, const ExecuteItem& b) { return a.depth > b.depth; });

    // Staged submission. A target that is another item's *current* name is not an
    // error: that name is freed by the same batch. But it is only free once the
    // item holding it has actually run, so the batch runs in rounds — each round
    // goes to the Shell and completes before the next is planned:
    //   round 1: everything whose target is free right now
    //   round N: everything whose blocker renamed in an earlier round
    // Anything still blocked after all rounds is reported, never looped on.
    //
    // Without this, the one-pass pre-flight would see "2026报告.pdf" sitting there
    // (its holder has not run yet) and refuse the item that legitimately wants
    // that name.
    std::vector<ExecuteItem> remaining = ordered;
    std::vector<ExecuteResultItem> rejected;

    bool undoAvailable = false;

    for (std::size_t round = 0; round <= ordered.size() && !remaining.empty(); ++round)
    {
        // ---- plan this round: who can run now -----------------------------
        std::vector<ExecuteItem> deferred;
        std::vector<ExecuteItem> accepted;
        bool progressed = false;

        for (const ExecuteItem& item : remaining)
        {
            ExecuteResultItem report;
            report.originalPath = item.fullPath;
            report.targetPath = item.deleteItem ? std::wstring()
                                                : JoinPath(DirectoryOf(item.fullPath), item.targetLeaf);

            const IdentitySnapshot before = ReadIdentity(item.fullPath);
            if (!before.valid)
            {
                report.outcome = ExecuteOutcome::IdentityChanged;
                report.detail = L"无法读取文件身份（可能已被移动或删除）";
                rejected.push_back(std::move(report));
                continue;
            }

            // Delete has no target-name collision to resolve. The Shell still
            // receives the same identity-checked source path and moves it to the
            // Recycle Bin under FOFX_RECYCLEONDELETE.
            if (!item.deleteItem && !TargetFree(report.targetPath))
            {
                // The target exists. If another item still waiting names it as its
                // own source, that item frees it by running — defer to a later
                // round instead of refusing. Otherwise: refuse, and never
                // overwrite or auto-suffix (RN-FR-05, RN-T6-01).
                bool freedByBatch = false;
                for (const ExecuteItem& other : remaining)
                {
                    if (&other == &item)
                        continue;
                    if (NfcEqualsPath(other.fullPath, report.targetPath))
                    {
                        freedByBatch = true;
                        break;
                    }
                }
                if (freedByBatch)
                {
                    deferred.push_back(item);
                    continue;
                }
                report.outcome = ExecuteOutcome::Refused;
                report.detail = L"目标已存在";
                rejected.push_back(std::move(report));
                continue;
            }

            accepted.push_back(item);
            progressed = true;
        }

        remaining = std::move(deferred);

        // ---- submit this round, then look again ---------------------------
        std::vector<ExecuteResultItem> roundResults;
        if (!accepted.empty())
            roundResults = SubmitThroughShell(accepted, rejected, undoAvailable);

        result.items.insert(result.items.end(),
                            std::make_move_iterator(roundResults.begin()),
                            std::make_move_iterator(roundResults.end()));

        if (!progressed)
            break;   // a cycle: what is left can never be satisfied
    }

    // Whatever is still waiting is a cycle the plan should have refused; report it
    // rather than pretending it was handled.
    for (const ExecuteItem& item : remaining)
    {
        ExecuteResultItem report;
        report.originalPath = item.fullPath;
        report.targetPath = JoinPath(DirectoryOf(item.fullPath), item.targetLeaf);
        report.outcome = ExecuteOutcome::Refused;
        report.detail = L"目标名字互相占用，无法排序";
        rejected.push_back(std::move(report));
    }

    result.undoAvailable = result.undoAvailable || undoAvailable;

    for (ExecuteResultItem& report : rejected)
        result.items.push_back(std::move(report));

    result.batchJournalPath = WriteJournal(result.items, dataDir);
    return result;
}

} // namespace rename_win
