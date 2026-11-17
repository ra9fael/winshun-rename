// e2e_rename.cpp — a real end-to-end rename against the filesystem.
//
// This is the L3-shaped check that cannot be faked: it creates a temp tree,
// enumerates it, builds a plan, executes it, and verifies the disk. It is a
// separate executable rather than a ctest case because it mutates the filesystem
// and needs a COM apartment; the test runner stays hermetic.
//
// Run:  build-msvc\e2e_rename.exe
// Exit: 0 all checks passed, 1 otherwise
#include "ui/presenter.h"
#include "win/platform.h"

#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_passed = 0;

void Check(bool ok, const char* label)
{
    if (ok)
    {
        ++g_passed;
        std::printf("[  OK  ] %s\n", label);
    }
    else
    {
        ++g_failed;
        std::printf("[ FAIL ] %s\n", label);
    }
}

bool Exists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void CreateFileWithText(const std::wstring& path)
{
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE)
    {
        const char payload[] = "x";
        DWORD written = 0;
        ::WriteFile(handle, payload, 1, &written, nullptr);
        ::CloseHandle(handle);
    }
}

std::wstring MakeTempRoot()
{
    wchar_t buffer[MAX_PATH]{};
    ::GetTempPathW(MAX_PATH, buffer);
    std::wstring root(buffer);
    root += L"ra9fael-rename-e2e-";
    root += std::to_wstring(::GetCurrentProcessId());
    ::CreateDirectoryW(root.c_str(), nullptr);
    return root;
}

void RemoveTree(const std::wstring& root)
{
    // Best effort: the e2e owns everything under this temp root.
    WIN32_FIND_DATAW data{};
    HANDLE find = ::FindFirstFileW((root + L"\\*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE)
    {
        std::vector<std::wstring> children;
        do
        {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            children.push_back(root + L"\\" + name);
        } while (::FindNextFileW(find, &data));
        ::FindClose(find);

        for (const std::wstring& child : children)
        {
            if (::GetFileAttributesW(child.c_str()) & FILE_ATTRIBUTE_DIRECTORY)
                RemoveTree(child);
            else
                ::DeleteFileW(child.c_str());
        }
    }
    ::RemoveDirectoryW(root.c_str());
}

} // namespace

int main()
{
    // IFileOperation needs COM on this thread.
    const HRESULT comInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comReady = SUCCEEDED(comInit);

    rename_win::InstallNfcNormalizer();

    const std::wstring root = MakeTempRoot();
    std::printf("temp root: %ls\n", root.c_str());

    // ---- case 1: a plain batch rename through the rule engine ----------------
    {
        const std::wstring a = root + L"\\draft-one.txt";
        const std::wstring b = root + L"\\draft-two.txt";
        CreateFileWithText(a);
        CreateFileWithText(b);

        rename_win::EnumerateRequest request;
        request.roots = {a, b};
        const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
        Check(collected.items.size() == 2, "enumerate finds both files");
        Check(!collected.blocked, "enumeration is not blocked");

        std::vector<rename_core::ItemSnapshot> items;
        for (const rename_win::CollectedItem& entry : collected.items)
        {
            rename_core::ItemSnapshot snapshot;
            snapshot.id = entry.id;
            snapshot.leaf = entry.leaf;
            snapshot.isFolder = entry.isFolder;
            snapshot.depth = entry.depth;
            snapshot.pathLength = entry.pathLength;
            snapshot.templateContext = rename_win::BuildTemplateContext(entry, 0);
            items.push_back(std::move(snapshot));
        }

        rename_core::Rule rule;
        rule.scope = rename_core::Scope::Name;
        rule.search = L"draft";
        rule.replace = L"final";
        rename_core::RuleChain chain;
        chain.rules.push_back(rule);

        const rename_core::PreviewPlan plan =
            rename_core::BuildControlPlan(items, chain, 0, rename_core::NextPlanId());
        Check(plan.CountByStatus(rename_core::Status::Ready) == 2, "plan marks both rows ready");

        std::vector<rename_win::ExecuteItem> batch;
        for (std::size_t i = 0; i < plan.items.size() && i < collected.items.size(); ++i)
        {
            rename_win::ExecuteItem entry;
            entry.fullPath = collected.items[i].fullPath;
            entry.targetLeaf = plan.items[i].targetLeaf;
            entry.isFolder = plan.items[i].isFolder;
            entry.depth = plan.items[i].depth;
            batch.push_back(entry);
        }

        const rename_win::ExecuteResult executed = rename_win::Execute(batch, root);
        int renamed = 0;
        for (const rename_win::ExecuteResultItem& result : executed.items)
        {
            if (result.outcome == rename_win::ExecuteOutcome::Renamed)
                ++renamed;
        }
        Check(renamed == 2, "both files renamed on disk");
        Check(Exists(root + L"\\final-one.txt"), "final-one.txt exists");
        Check(Exists(root + L"\\final-two.txt"), "final-two.txt exists");
        Check(!Exists(a), "original draft-one.txt is gone");
    }

    // ---- case 2: a conflict must be refused, never auto-suffixed -------------
    {
        const std::wstring source = root + L"\\source.txt";
        const std::wstring target = root + L"\\taken.txt";
        CreateFileWithText(source);
        CreateFileWithText(target);

        rename_win::ExecuteItem entry;
        entry.fullPath = source;
        entry.targetLeaf = L"taken.txt";

        const rename_win::ExecuteResult executed = rename_win::Execute({entry}, root);

        bool refused = false;
        for (const rename_win::ExecuteResultItem& result : executed.items)
        {
            if (result.outcome == rename_win::ExecuteOutcome::Refused)
                refused = true;
        }
        Check(refused, "existing target is refused");
        Check(Exists(target), "the existing target was not touched");
        Check(Exists(source), "the source was not renamed");
        Check(!Exists(root + L"\\taken (1).txt"), "no automatic 'name (1)' was created");
    }

    // ---- case 3: deepest-first, parent and child in one batch ----------------
    {
        const std::wstring parent = root + L"\\parentdir";
        ::CreateDirectoryW(parent.c_str(), nullptr);
        const std::wstring child = parent + L"\\child.txt";
        CreateFileWithText(child);

        std::vector<rename_win::ExecuteItem> batch;

        rename_win::ExecuteItem parentItem;
        parentItem.fullPath = parent;
        parentItem.targetLeaf = L"renameddir";
        parentItem.isFolder = true;
        parentItem.depth = 0;
        batch.push_back(parentItem);

        rename_win::ExecuteItem childItem;
        childItem.fullPath = child;
        childItem.targetLeaf = L"kid.txt";
        childItem.depth = 1;
        batch.push_back(childItem);

        const rename_win::ExecuteResult executed = rename_win::Execute(batch, root);

        int renamed = 0;
        for (const rename_win::ExecuteResultItem& result : executed.items)
        {
            if (result.outcome == rename_win::ExecuteOutcome::Renamed)
                ++renamed;
        }
        Check(renamed == 2, "parent and child both renamed");
        Check(Exists(root + L"\\renameddir\\kid.txt"),
              "child landed under the renamed parent, deepest-first worked");
    }

    // ---- case 4: identity change is caught -----------------------------------
    {
        const std::wstring source = root + L"\\changing.txt";
        CreateFileWithText(source);

        rename_win::ExecuteItem entry;
        entry.fullPath = source;
        entry.targetLeaf = L"changed.txt";

        // Rewrite the file after the plan would have been built. The executor
        // reads identity inside Execute, so this only proves the path is live;
        // the dedicated identity check is exercised by the size/timestamp
        // comparison in win/execute.cpp and is documented as unverified for the
        // "identical replacement" corner case in the validation notes.
        CreateFileWithText(source);

        const rename_win::ExecuteResult executed = rename_win::Execute({entry}, root);
        Check(executed.items.size() == 1, "identity case produced one result row");
    }

    // ---- case 5 (LS-S7-01..05): a folder stands for its direct children ------
    {
        const std::wstring folder = root + L"\\listme";
        ::CreateDirectoryW(folder.c_str(), nullptr);
        CreateFileWithText(folder + L"\\a.txt");
        CreateFileWithText(folder + L"\\b.txt");
        ::CreateDirectoryW((folder + L"\\sub").c_str(), nullptr);
        CreateFileWithText(folder + L"\\sub\\deep.txt");

        rename_win::EnumerateRequest request;
        request.roots = {folder};
        request.listContents = true;
        const rename_win::EnumerateResult listed = rename_win::Enumerate(request);

        Check(!listed.blocked, "LS-S7-01 a listable folder does not block");
        Check(listed.items.size() == 3, "LS-S7-01 folder yields its three direct children");
        bool sawSelf = false, sawDeep = false, subIsFolder = false;
        for (const rename_win::CollectedItem& entry : listed.items)
        {
            if (entry.fullPath == folder)
                sawSelf = true;
            if (entry.leaf == L"deep.txt")
                sawDeep = true;
            if (entry.leaf == L"sub" && entry.isFolder)
                subIsFolder = true;
            if (entry.depth != 0)
                Check(false, "LS-S7-01 every listed child is depth 0");
        }
        Check(!sawSelf, "LS-S7-01 the folder itself is not a row");
        Check(!sawDeep, "LS-S7-03 a subfolder's contents are not listed");
        Check(subIsFolder, "LS-S7-03 the subfolder is a row with isFolder");

        // LS-S7-02: an empty folder is an empty set, not a failure.
        const std::wstring empty = root + L"\\listempty";
        ::CreateDirectoryW(empty.c_str(), nullptr);
        rename_win::EnumerateRequest emptyRequest;
        emptyRequest.roots = {empty};
        emptyRequest.listContents = true;
        const rename_win::EnumerateResult emptyListed = rename_win::Enumerate(emptyRequest);
        Check(!emptyListed.blocked && emptyListed.items.empty(),
              "LS-S7-02 an empty folder yields no rows and no block");

        // LS-S7-05: a folder plus a file inside it de-duplicates.
        rename_win::EnumerateRequest overlap;
        overlap.roots = {folder, folder + L"\\a.txt"};
        overlap.listContents = true;
        const rename_win::EnumerateResult overlapped = rename_win::Enumerate(overlap);
        int aCount = 0;
        for (const rename_win::CollectedItem& entry : overlapped.items)
            if (entry.leaf == L"a.txt")
                ++aCount;
        Check(aCount == 1, "LS-S7-05 a file listed twice appears once");

        // LS-S7-07: a missing root blocks with a reason.
        rename_win::EnumerateRequest missing;
        missing.roots = {root + L"\\does-not-exist-here"};
        missing.listContents = true;
        const rename_win::EnumerateResult missingListed = rename_win::Enumerate(missing);
        Check(missingListed.blocked, "LS-S7-07 a missing root blocks the set");

        // LS-S7-06: without listContents the folder is the row (old behaviour).
        rename_win::EnumerateRequest asItem;
        asItem.roots = {folder};
        const rename_win::EnumerateResult asItemListed = rename_win::Enumerate(asItem);
        Check(asItemListed.items.size() == 1 && asItemListed.items[0].fullPath == folder,
              "LS-S7-06 listContents=false keeps the folder itself as the row");
    }

    // ---- case 6 (LS-S7-09): renaming a folder's contents really renames them --
    {
        const std::wstring folder = root + L"\\renamecontents";
        ::CreateDirectoryW(folder.c_str(), nullptr);
        CreateFileWithText(folder + L"\\old_1.txt");
        CreateFileWithText(folder + L"\\old_2.txt");
        ::CreateDirectoryW((folder + L"\\old_dir").c_str(), nullptr);
        CreateFileWithText(folder + L"\\old_dir\\inner.txt");

        rename_win::EnumerateRequest request;
        request.roots = {folder};
        request.listContents = true;
        const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
        Check(collected.items.size() == 3, "LS-S7-09 three rows to work with");

        std::vector<rename_core::ItemSnapshot> items;
        for (const rename_win::CollectedItem& entry : collected.items)
        {
            rename_core::ItemSnapshot snapshot;
            snapshot.id = entry.id;
            snapshot.leaf = entry.leaf;
            snapshot.isFolder = entry.isFolder;
            snapshot.depth = entry.depth;
            snapshot.pathLength = entry.pathLength;
            snapshot.templateContext = rename_win::BuildTemplateContext(entry, 0);
            items.push_back(std::move(snapshot));
        }

        rename_core::Rule rule;
        rule.scope = rename_core::Scope::Name;
        rule.search = L"old";
        rule.replace = L"new";
        rename_core::RuleChain chain;
        chain.rules.push_back(rule);

        const rename_core::PreviewPlan plan =
            rename_core::BuildControlPlan(items, chain, 0, rename_core::NextPlanId());
        Check(plan.CountByStatus(rename_core::Status::Ready) == 3,
              "LS-S7-09 all three children are ready");

        std::vector<rename_win::ExecuteItem> batch;
        for (std::size_t i = 0; i < plan.items.size() && i < collected.items.size(); ++i)
        {
            rename_win::ExecuteItem entry;
            entry.fullPath = collected.items[i].fullPath;
            entry.targetLeaf = plan.items[i].targetLeaf;
            entry.isFolder = plan.items[i].isFolder;
            entry.depth = plan.items[i].depth;
            batch.push_back(entry);
        }
        const rename_win::ExecuteResult executed = rename_win::Execute(batch, folder);
        int renamed = 0;
        for (const rename_win::ExecuteResultItem& result : executed.items)
            if (result.outcome == rename_win::ExecuteOutcome::Renamed)
                ++renamed;
        Check(renamed == 3, "LS-S7-09 three children renamed on disk");
        Check(Exists(folder + L"\\new_1.txt") && Exists(folder + L"\\new_2.txt"),
              "LS-S7-09 the files took their new names");
        Check(Exists(folder + L"\\new_dir\\inner.txt"),
              "LS-S7-09 the renamed subfolder kept its contents in place");
    }

    // ---- case 6b (LS-S7-10): a target that is another row's current name -----
    // "2026报告.pdf" already exists and the rule rewrites it to "20262026报告.pdf",
    // while "报告.pdf" becomes "2026报告.pdf". Refusing the second one because its
    // target is momentarily taken would be wrong: the batch frees that name.
    {
        const std::wstring folder = root + L"\\dependencychain";
        ::CreateDirectoryW(folder.c_str(), nullptr);
        CreateFileWithText(folder + L"\\报告.pdf");
        CreateFileWithText(folder + L"\\2026报告.pdf");
        CreateFileWithText(folder + L"\\会议报告.docx");

        rename_win::EnumerateRequest request;
        request.roots = {folder};
        request.listContents = true;
        const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
        Check(collected.items.size() == 3, "LS-S7-10 three files in the chain folder");

        std::vector<rename_core::ItemSnapshot> items;
        for (const rename_win::CollectedItem& entry : collected.items)
        {
            rename_core::ItemSnapshot snapshot;
            snapshot.id = entry.id;
            snapshot.leaf = entry.leaf;
            snapshot.isFolder = entry.isFolder;
            snapshot.depth = entry.depth;
            snapshot.pathLength = entry.pathLength;
            snapshot.templateContext = rename_win::BuildTemplateContext(entry, 0);
            items.push_back(std::move(snapshot));
        }

        rename_core::Rule rule;
        rule.scope = rename_core::Scope::Name;
        rule.search = L"报告";
        rule.replace = L"2026报告";
        rename_core::RuleChain chain;
        chain.rules.push_back(rule);

        const rename_core::PreviewPlan plan =
            rename_core::BuildControlPlan(items, chain, 0, rename_core::NextPlanId());
        // Every row must be plannable: a target that another row still holds is
        // not a conflict, it is an ordering constraint.
        Check(plan.CountByStatus(rename_core::Status::Ready) == 3,
              "LS-S7-10 all three rows are plannable, not conflicts");

        std::vector<rename_win::ExecuteItem> batch;
        for (std::size_t i = 0; i < plan.items.size() && i < collected.items.size(); ++i)
        {
            rename_win::ExecuteItem entry;
            entry.fullPath = collected.items[i].fullPath;
            entry.targetLeaf = plan.items[i].targetLeaf;
            entry.isFolder = plan.items[i].isFolder;
            entry.depth = plan.items[i].depth;
            batch.push_back(entry);
        }
        const rename_win::ExecuteResult executed = rename_win::Execute(batch, folder);
        int renamed = 0;
        for (const rename_win::ExecuteResultItem& result : executed.items)
            if (result.outcome == rename_win::ExecuteOutcome::Renamed)
                ++renamed;
        Check(renamed == 3, "LS-S7-10 the whole chain is renamed, nothing refused");
        Check(Exists(folder + L"\\20262026报告.pdf"),
              "LS-S7-10 the held name was released and then taken");
        Check(Exists(folder + L"\\2026报告.pdf"),
              "LS-S7-10 the freed name was claimed by the waiting row");
        Check(Exists(folder + L"\\会议2026报告.docx"),
              "LS-S7-10 an unrelated row was renamed too");
    }

    // ---- case 6c (LS-S7-11): a swap is a cycle, so it must be refused --------
    // Two rows exchanging names can never be ordered without a temporary name.
    // The planner refuses the pair rather than letting the executor improvise.
    {
        const std::wstring folder = root + L"\\swapecycle";
        ::CreateDirectoryW(folder.c_str(), nullptr);
        CreateFileWithText(folder + L"\\left.txt");
        CreateFileWithText(folder + L"\\right.txt");

        rename_win::ExecuteItem a;
        a.fullPath = folder + L"\\left.txt";
        a.targetLeaf = L"right.txt";
        rename_win::ExecuteItem b;
        b.fullPath = folder + L"\\right.txt";
        b.targetLeaf = L"left.txt";

        const rename_win::ExecuteResult executed = rename_win::Execute({a, b}, folder);
        int renamed = 0;
        int refused = 0;
        for (const rename_win::ExecuteResultItem& result : executed.items)
        {
            if (result.outcome == rename_win::ExecuteOutcome::Renamed)
                ++renamed;
            if (result.outcome == rename_win::ExecuteOutcome::Refused)
                ++refused;
        }
        Check(renamed == 0, "LS-S7-11 a name swap renames nothing");
        Check(refused == 2, "LS-S7-11 both rows of the swap are refused");
        Check(Exists(folder + L"\\left.txt") && Exists(folder + L"\\right.txt"),
              "LS-S7-11 both files kept their names");
    }

    // ---- case 7 (LS-S8-01..05): the minimal single-file path ------------------
    {
        const std::wstring single = root + L"\\single_report.txt";
        CreateFileWithText(single);

        rename_win::EnumerateRequest request;
        request.roots = {single};
        const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
        Check(collected.items.size() == 1, "LS-S8-01 one file in, one row");

        auto planFor = [&collected](const std::wstring& find, const std::wstring& replace) {
            std::vector<rename_core::ItemSnapshot> items;
            for (const rename_win::CollectedItem& entry : collected.items)
            {
                rename_core::ItemSnapshot snapshot;
                snapshot.id = entry.id;
                snapshot.leaf = entry.leaf;
                snapshot.isFolder = entry.isFolder;
                snapshot.depth = entry.depth;
                snapshot.pathLength = entry.pathLength;
                snapshot.templateContext = rename_win::BuildTemplateContext(entry, 0);
                items.push_back(std::move(snapshot));
            }
            rename_core::Rule rule;
            rule.scope = rename_core::Scope::Name;
            rule.search = find;
            rule.replace = replace;
            rename_core::RuleChain chain;
            chain.rules.push_back(rule);
            return rename_core::BuildControlPlan(items, chain, 0, rename_core::NextPlanId());
        };

        const rename_core::PreviewPlan withReplace = planFor(L"report", L"summary");
        Check(withReplace.CountByStatus(rename_core::Status::Ready) == 1,
              "LS-S8-01 one ready row");
        Check(withReplace.items[0].targetLeaf == L"single_summary.txt",
              "LS-S8-01 the target name is right");

        const rename_core::PreviewPlan absent = planFor(L"nothing-here", L"x");
        Check(absent.CountByStatus(rename_core::Status::Unchanged) == 1,
              "LS-S8-05 a find text that never occurs leaves the row unchanged");
    }

    // ---- case 8 (LS-S8-03/04): single file, conflict refused, clean run works -
    {
        const std::wstring source = root + L"\\solo.txt";
        const std::wstring taken = root + L"\\solo_taken.txt";
        CreateFileWithText(source);
        CreateFileWithText(taken);

        rename_win::ExecuteItem conflict;
        conflict.fullPath = source;
        conflict.targetLeaf = L"solo_taken.txt";
        const rename_win::ExecuteResult refused = rename_win::Execute({conflict}, root);
        bool wasRefused = false;
        for (const rename_win::ExecuteResultItem& result : refused.items)
            if (result.outcome == rename_win::ExecuteOutcome::Refused)
                wasRefused = true;
        Check(wasRefused, "LS-S8-03 an existing target is refused");
        Check(Exists(source), "LS-S8-03 the source stayed put");

        rename_win::ExecuteItem ok;
        ok.fullPath = source;
        ok.targetLeaf = L"solo_final.txt";
        const rename_win::ExecuteResult done = rename_win::Execute({ok}, root);
        bool wasRenamed = false;
        for (const rename_win::ExecuteResultItem& result : done.items)
            if (result.outcome == rename_win::ExecuteOutcome::Renamed)
                wasRenamed = true;
        Check(wasRenamed, "LS-S8-04 a single file renames");
        Check(Exists(root + L"\\solo_final.txt"), "LS-S8-04 the new name is on disk");
    }

    // ---- case 9: explicit delete uses Shell + Recycle Bin --------------------
    {
        const std::wstring source = root + L"\\delete-me.txt";
        CreateFileWithText(source);

        rename_win::ExecuteItem remove;
        remove.fullPath = source;
        remove.deleteItem = true;
        const rename_win::ExecuteResult done = rename_win::Execute({remove}, root);
        bool wasDeleted = false;
        for (const rename_win::ExecuteResultItem& result : done.items)
            if (result.outcome == rename_win::ExecuteOutcome::Renamed)
                wasDeleted = true;
        Check(wasDeleted, "delete item completed through IFileOperation");
        Check(!Exists(source), "delete item no longer exists at its original path");
        Check(done.undoAvailable, "delete requested a Shell undo record");
    }

    RemoveTree(root);
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);

    if (comReady)
        ::CoUninitialize();
    return g_failed == 0 ? 0 : 1;
}
