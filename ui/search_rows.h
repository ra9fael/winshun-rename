// search_rows.h — what `rename <words>` shows in the keyword list.
//
// The keyword box is WinShun's search interface: after the keyword, every
// keystroke is a query, and the plugin asks WinShun's own index for the names
// it matches. This turns the answer into rows: one that opens the batch on
// every match, then the first few matches as one-file batches. Plain C++ with
// no Qt, so the row decisions are testable without a host and without a
// window. The host side (tools/search) is main.cpp's job.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rename_ui {

// One name WinShun's index answered with. Paths are UTF-8 and absolute.
struct SearchHit {
    std::string path;
    std::string name;
};

struct SearchRow {
    enum class Kind { All, One };
    Kind kind = Kind::One;
    std::string title;
    std::string subtitle;
    // All: the whole batch, in the index's order. One: that single file.
    std::vector<std::string> paths;
};

// The folder a path sits in: "C:\a\b.txt" -> "C:\a", "C:\a" -> "C:\".
// Empty when the path has no folder part.
std::string ParentFolder(const std::string& path);

// `hits` is empty -> no rows (the caller says why). Otherwise the batch row
// comes first, then at most `shown` single-file rows: a search that returns
// hundreds of names must not bury the box, and Enter on the first row is the
// intended path for "rename all of them".
std::vector<SearchRow> BuildSearchRows(const std::string& words,
                                       const std::vector<SearchHit>& hits,
                                       bool truncated,
                                       bool chinese,
                                       std::size_t shown = 10);

} // namespace rename_ui
