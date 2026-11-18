#include "ui/search_rows.h"

#include <algorithm>

namespace rename_ui {

namespace {

std::string Count(std::size_t n)
{
    return std::to_string(n);
}

} // namespace

std::string ParentFolder(const std::string& path)
{
    std::size_t end = path.size();
    while (end > 0 && (path[end - 1] == '\\' || path[end - 1] == '/'))
        --end;
    if (end == 0)
        return {};
    const std::size_t slash = path.find_last_of("\\/", end - 1);
    if (slash == std::string::npos)
        return {};
    if (slash == 2 && path.size() >= 3 && path[1] == ':')
        return path.substr(0, 3); // "C:\"
    return path.substr(0, slash);
}

std::vector<SearchRow> BuildSearchRows(const std::string& words,
                                       const std::vector<SearchHit>& hits,
                                       bool truncated,
                                       bool chinese,
                                       std::size_t shown)
{
    if (hits.empty())
        return {};

    std::vector<SearchRow> rows;
    rows.reserve(1 + std::min(shown, hits.size()));

    SearchRow all;
    all.kind = SearchRow::Kind::All;
    all.title = chinese ? "批量重命名匹配的 " + Count(hits.size()) + " 项…"
                        : "Batch rename the " + Count(hits.size()) + " matches…";
    all.subtitle = (chinese ? "搜索：" : "Search: ") + words;
    if (truncated)
        all.subtitle += chinese ? " · 还有更多匹配没有列出" : " · more matched than listed";
    for (const SearchHit& hit : hits)
        all.paths.push_back(hit.path);
    rows.push_back(std::move(all));

    for (std::size_t i = 0; i < std::min(shown, hits.size()); ++i)
    {
        SearchRow one;
        one.kind = SearchRow::Kind::One;
        one.title = hits[i].name;
        one.subtitle = ParentFolder(hits[i].path);
        one.paths.push_back(hits[i].path);
        rows.push_back(std::move(one));
    }

    return rows;
}

} // namespace rename_ui
