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

// Depth-first collection. `depth` is the distance from the root that produced it,
// which is exactly what the executor's deepest-first ordering needs.
void CollectInto(const std::wstring& root,
                 const std::string& parentIdPrefix,
                 unsigned depth,
                 bool recursive,
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

    if (!recursive)
    {
        // Single item: just record it.
        CollectedItem entry;
        entry.id = std::to_string(++counter);
        entry.fullPath = root;
        entry.leaf = LeafOf(root);
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
        CollectInto(child, parentIdPrefix, depth + 1, recursive, result, seen, counter);
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
        CollectInto(root, {}, 0, request.recursive, result, seen, counter);
    }

    return result;
}

} // namespace rename_win
