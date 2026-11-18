// enumerate.cpp — turn host paths into item snapshots (design §3.3, RN-FR-14).
//
// Three rules that matter, all from the design:
//
//   * Recursion is off by default. Turning it on is the only thing that makes
//     this function walk a directory (RN-FR-14); a user who never asked for it
//     must never pay for a traversal.
//   * Never follow symlinks or junctions, and never leave the requested root.
//     A rename tool that wanders out of the selection is a data-loss tool.
//   * A read failure blocks the set instead of silently renaming an incomplete
//     one. Half a batch is worse than no batch.
#include "win/platform.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <set>
#include <string>

namespace rename_win {

namespace {

std::wstring LeafOf(const std::wstring& path)
{
    if (path.empty())
        return path;
    std::size_t end = path.size();
    // Tolerate a trailing separator ("C:\dir\") by trimming it before the split.
    while (end > 1 && (path[end - 1] == L'\\' || path[end - 1] == L'/'))
        --end;
    const std::size_t slash = path.find_last_of(L"\\/", end == 0 ? 0 : end - 1);
    if (slash == std::wstring::npos)
        return path.substr(0, end);
    return path.substr(slash + 1, end - slash - 1);
}

// The folder a path sits in. The plan compares targets within one folder, so
// this is the identity of "where this row will land" (core/plan.cpp).
std::wstring FolderOf(const std::wstring& path)
{
    std::wstring trimmed = path;
    while (trimmed.size() > 1 && (trimmed.back() == L'\\' || trimmed.back() == L'/'))
        trimmed.pop_back();
    const std::size_t slash = trimmed.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash == 0)
        return {};
    if (slash == 2 && trimmed.size() >= 3 && trimmed[1] == L':')
        return trimmed.substr(0, 3); // "C:\"
    return trimmed.substr(0, slash);
}

bool IsDirectory(const std::wstring& path)
{
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return false;
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsReparsePoint(const std::wstring& path)
{
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return false;
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

std::wstring FoldKey(const std::wstring& path)
{
    std::wstring out(path);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch))); });
    return out;
}

// Forward declaration: the two collectors call each other.
void CollectInto(const std::wstring& root,
                 const std::string& parentIdPrefix,
                 unsigned depth,
                 bool recursive,
                 bool listContents,
                 EnumerateResult& result,
                 std::set<std::wstring>& seen,
                 int& counter);

// The direct children of `folder`, each as a row at depth 0: "here is what you
// are working on" rather than "this folder is one of them". Subfolders join as
// rows; whether their contents follow is `recursive`'s decision and nothing
// else's. A link is a row but never entered, and an unreadable folder blocks the
// set — the same rules the recursive path holds to.
void ListChildren(const std::wstring& folder,
                  EnumerateResult& result,
                  std::set<std::wstring>& seen,
                  int& counter,
                  bool recursive,
                  unsigned depth)
{
    // "C:\" and "C:\dir\" both name a folder; a doubled separator would make the
    // search pattern invalid on a drive root.
    std::wstring base = folder;
    while (base.size() > 1 && (base.back() == L'\\' || base.back() == L'/'))
        base.pop_back();

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = base + L"\\*";
    HANDLE find = ::FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        const DWORD error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
            return;   // nothing in it: an empty set, not a failure
        result.blocked = true;
        result.blockReason = L"无法读取目录：" + folder;
        return;
    }

    std::vector<std::wstring> subfolders;
    do
    {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
            continue;

        std::wstring child = base;
        if (!child.empty() && child.back() != L'\\' && child.back() != L'/')
            child += L'\\';
        child += name;
        const bool childIsFolder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isLink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        CollectedItem entry;
        entry.id = std::to_string(++counter);
        entry.fullPath = child;
        entry.leaf = name;
        entry.parent = base;
        entry.isFolder = childIsFolder;
        // Depth 0 for everything: a folder's children have no parent *in this
        // batch*, so renaming the folder must not reorder around them.
        entry.depth = depth;
        entry.pathLength = child.size();

        const std::wstring key = FoldKey(child);
        if (!seen.insert(key).second)
            continue;
        result.items.push_back(std::move(entry));

        if (isLink)
        {
            // A row the user can rename, but its target is not part of the set.
            result.notes.push_back(L"跳过链接、不进入：" + name);
            continue;
        }
        if (childIsFolder)
            subfolders.push_back(child);
    } while (::FindNextFileW(find, &data));
    ::FindClose(find);

    if (!recursive)
        return;
    for (const std::wstring& sub : subfolders)
        CollectInto(sub, {}, depth + 1, true, false, result, seen, counter);
}

// Depth-first collection. `depth` is the distance from the root that produced it,
// which is exactly what the executor's deepest-first ordering needs.
void CollectInto(const std::wstring& root,
                 const std::string& parentIdPrefix,
                 unsigned depth,
                 bool recursive,
                 bool listContents,
                 EnumerateResult& result,
                 std::set<std::wstring>& seen,
                 int& counter)
{
    if (depth > 64)
    {
        // A runaway depth means something pathological (or a link loop we failed
        // to detect). Refuse rather than recurse forever.
        result.blocked = true;
        result.blockReason = L"目录层级过深，已停止遍历";
        return;
    }

    // A root that does not exist is a broken set, not a row to rename: the user
    // asked for something that is not there, and renaming a name that names
    // nothing would fail per item much later.
    if (::GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        result.blocked = true;
        result.blockReason = L"找不到：" + root;
        return;
    }

    // "Open the window in this folder": a folder root stands for its entries, so
    // the user works on what is inside it rather than on the folder itself.
    // Recursion is a separate decision, still off unless asked for.
    if (listContents && depth == 0 && IsDirectory(root))
    {
        ListChildren(root, result, seen, counter, recursive, depth);
        return;
    }

    if (!recursive)
    {
        // Single item: just record it.
        CollectedItem entry;
        entry.id = std::to_string(++counter);
        entry.fullPath = root;
        entry.leaf = LeafOf(root);
        entry.parent = FolderOf(root);
        entry.isFolder = IsDirectory(root);
        entry.depth = depth;
        entry.pathLength = root.size();

        const std::wstring key = FoldKey(root);
        if (seen.insert(key).second)
            result.items.push_back(std::move(entry));
        return;
    }

    if (IsReparsePoint(root))
    {
        // Never traverse a link: it can point anywhere, including outside the
        // selection, and the user did not ask to rename its target.
        result.notes.push_back(L"跳过链接：" + root);
        return;
    }

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = root + L"\\*";
    HANDLE find = ::FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        const DWORD error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
            return;   // empty directory, not a failure
        result.blocked = true;
        result.blockReason = L"无法读取目录：" + root;
        return;
    }

    std::vector<std::wstring> children;
    do
    {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
            continue;

        const std::wstring child = root + L"\\" + name;
        const bool childIsFolder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isLink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        CollectedItem entry;
        entry.id = std::to_string(++counter);
        entry.fullPath = child;
        entry.leaf = name;
        entry.parent = FolderOf(child);
        entry.isFolder = childIsFolder;
        entry.depth = depth + 1;
        entry.pathLength = child.size();

        const std::wstring key = FoldKey(child);
        if (seen.insert(key).second)
        {
            result.items.push_back(std::move(entry));
            if (childIsFolder && !isLink)
                children.push_back(child);
        }
    } while (::FindNextFileW(find, &data));
    ::FindClose(find);

    for (const std::wstring& child : children)
        CollectInto(child, parentIdPrefix, depth + 1, recursive, false, result, seen, counter);
}

} // namespace

EnumerateResult Enumerate(const EnumerateRequest& request)
{
    EnumerateResult result;

    if (request.roots.empty())
    {
        result.blocked = true;
        result.blockReason = L"没有选中任何文件";
        return result;
    }

    std::set<std::wstring> seen;
    int counter = 0;

    // Overlapping roots must not produce duplicate rows: renaming the same file
    // twice in one batch is a guaranteed conflict, so de-duplicate by folded path
    // the same way Windows compares names.
    for (const std::wstring& root : request.roots)
    {
        if (root.empty())
            continue;
        CollectInto(root, {}, 0, request.recursive, request.listContents, result, seen, counter);
    }

    // A blocked set is returned empty even when earlier roots were read: those
    // rows are not the selection the user made, and a caller that took them
    // would work on half a batch without saying so.
    if (result.blocked)
        result.items.clear();

    return result;
}

} // namespace rename_win
