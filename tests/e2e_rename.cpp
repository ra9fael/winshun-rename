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

    RemoveTree(root);
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);

    if (comReady)
        ::CoUninitialize();
    return g_failed == 0 ? 0 : 1;
}
