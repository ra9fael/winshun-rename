// test_search_rows.cpp — the rows `rename <words>` shows.
//
// The host side (WinShun's index, the protocol) is main.cpp's and host-sim's
// business; this is the decision the rows make, which is the part that can be
// wrong without a host to blame.
#include "ui/search_rows.h"
#include "tests/test_harness.h"

#include <string>
#include <vector>

using namespace rename_ui;

namespace {

SearchHit hit(const char* path, const char* name)
{
    return SearchHit{path, name};
}

} // namespace

TEST_CASE(ParentFolderSplitsAPath)
{
    CHECK_EQ(ParentFolder("D:\\work\\report.txt"), std::string("D:\\work"));
    CHECK_EQ(ParentFolder("D:\\work\\sub"), std::string("D:\\work"));
    CHECK_EQ(ParentFolder("D:\\work\\"), std::string("D:\\"));
    CHECK_EQ(ParentFolder("D:\\"), std::string());
    CHECK_EQ(ParentFolder("report.txt"), std::string());
}

TEST_CASE(SearchRowsOpenTheWholeBatchFirst)
{
    const std::vector<SearchHit> hits = {
        hit("D:\\work\\a.txt", "a.txt"),
        hit("D:\\other\\b.txt", "b.txt"),
    };
    const std::vector<SearchRow> rows = BuildSearchRows("txt", hits, false, true);

    CHECK_EQ(rows.size(), std::size_t(3));
    CHECK(rows[0].kind == SearchRow::Kind::All);
    CHECK_EQ(rows[0].paths.size(), std::size_t(2));
    CHECK_EQ(rows[0].paths[1], std::string("D:\\other\\b.txt"));
    CHECK(rows[0].title.find("2") != std::string::npos);

    CHECK(rows[1].kind == SearchRow::Kind::One);
    CHECK_EQ(rows[1].title, std::string("a.txt"));
    CHECK_EQ(rows[1].subtitle, std::string("D:\\work"));
    CHECK_EQ(rows[1].paths.size(), std::size_t(1));
}

TEST_CASE(SearchRowsCapThePerFileRows)
{
    // A search that answers with hundreds of names must not bury the box: the
    // batch row still carries every one of them.
    std::vector<SearchHit> hits;
    for (int i = 0; i < 25; ++i)
        hits.push_back(hit("D:\\work\\f.txt", "f.txt"));

    const std::vector<SearchRow> rows = BuildSearchRows("f", hits, true, false, 10);
    CHECK_EQ(rows.size(), std::size_t(11));
    CHECK_EQ(rows[0].paths.size(), std::size_t(25));
    CHECK(rows[0].subtitle.find("more matched") != std::string::npos);
}

TEST_CASE(SearchRowsAreEmptyWithoutHits)
{
    CHECK(BuildSearchRows("nothing", {}, false, true).empty());
}
