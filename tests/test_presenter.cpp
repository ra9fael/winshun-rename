// test_presenter.cpp — L2 tests for the view-model (M3/M4).
//
// Covers, per TEST-PLAN.md:
//   RN-T2-01  filtering never changes the input set; selection affects the plan only
//   RN-T2-05  deepest-first execution order
//   RN-T2-06  both modes agree on the status of the same name
//   RN-T2-07  a text buffer mixing renames with deletes is refused wholesale
//   RN-T3-05  (partial) the buffer resets after a batch so a second edit is safe
//
// Nothing here touches the filesystem: the presenter takes snapshots and returns
// a plan, which is exactly the seam the Qt layer will sit on top of.
#include "ui/presenter.h"
#include "tests/test_harness.h"

#include <string>

using namespace rename_core;
using namespace rename_ui;

// This file has no main(): test_core.cpp owns it for the single test binary.

namespace {

ItemSnapshot item(const char* id, const wchar_t* leaf, unsigned depth = 0, bool folder = false)
{
    ItemSnapshot snapshot;
    snapshot.id = id;
    snapshot.leaf = leaf;
    snapshot.isFolder = folder;
    snapshot.depth = depth;
    snapshot.pathLength = 20 + snapshot.leaf.size();
    snapshot.templateContext.date.year = 2026;
    snapshot.templateContext.date.month = 3;
    snapshot.templateContext.date.day = 7;
    snapshot.templateContext.monthLong = L"March";
    snapshot.templateContext.monthShort = L"Mar";
    snapshot.templateContext.weekdayLong = L"Saturday";
    snapshot.templateContext.weekdayShort = L"Sat";
    return snapshot;
}

RuleChain renameRule(std::wstring search, std::wstring replace)
{
    Rule rule;
    rule.scope = Scope::Name;
    rule.search = std::move(search);
    rule.replace = std::move(replace);
    RuleChain chain;
    chain.rules.push_back(std::move(rule));
    return chain;
}

std::vector<OilEntry> oilTable()
{
    OilEntry a;
    a.id = 1;
    a.originalLeaf = L"one.txt";
    a.path = L"C:\\tmp\\one.txt";

    OilEntry b;
    b.id = 2;
    b.originalLeaf = L"two.txt";
    b.path = L"C:\\tmp\\two.txt";

    return {a, b};
}

} // namespace

TEST_CASE(PresenterStartsIdle)
{
    Presenter presenter;
    CHECK_EQ(presenter.CurrentPhase(), Phase::Idle);
    CHECK(!presenter.CanExecute());
    CHECK_EQ(presenter.BlockingReason(), std::wstring(L"no-files"));
}

TEST_CASE(PresenterBuildsControlPlan)
{
    Presenter presenter;
    ControlState control;
    control.chain = renameRule(L"draft", L"final");
    presenter.SetControlState(control);
    presenter.SetItems({item("1", L"draft.txt"), item("2", L"notes.txt")});

    const Counts counts = presenter.CurrentCounts();
    CHECK_EQ(counts.ready, 1);
    CHECK_EQ(counts.unchanged, 1);
    CHECK(presenter.CanExecute());
}

TEST_CASE(PresenterFilterNeverChangesInputSet)
{
    // The rule from RN-FR-10: filtering is a view concern. If it changed the input
    // set, a user who filtered to "errors" and hit Apply would rename a subset.
    Presenter presenter;
    ControlState control;
    control.chain = renameRule(L"a", L"b");
    presenter.SetControlState(control);
    presenter.SetItems({item("1", L"a1.txt"), item("2", L"a2.txt"), item("3", L"zzz.txt")});

    const std::size_t planSizeBefore = presenter.Plan().items.size();
    presenter.SetFilter(Filter::ToRename);
    const std::size_t visible = presenter.VisibleRows().size();
    const std::size_t planSizeAfter = presenter.Plan().items.size();

    CHECK_EQ(planSizeBefore, planSizeAfter);
    CHECK(visible < planSizeAfter);   // the filter did narrow the view
    // ...but the batch is still two items, not the visible count.
    CHECK_EQ(presenter.ExecutionOrder().size(), std::size_t(2));
}

TEST_CASE(PresenterSelectionAffectsExecutionNotPlanSize)
{
    Presenter presenter;
    ControlState control;
    control.chain = renameRule(L"a", L"b");
    presenter.SetControlState(control);
    presenter.SetItems({item("1", L"a1.txt"), item("2", L"a2.txt")});

    presenter.SetRowSelected("2", false);
    CHECK_EQ(presenter.Plan().items.size(), std::size_t(2));
    CHECK_EQ(presenter.ExecutionOrder().size(), std::size_t(1));
}

TEST_CASE(PresenterSelectionSurvivesRebuild)
{
    // A user who unchecked a row should not have to redo it after every keystroke.
    Presenter presenter;
    ControlState control;
    control.chain = renameRule(L"a", L"b");
    presenter.SetControlState(control);
    presenter.SetItems({item("1", L"a1.txt"), item("2", L"a2.txt")});
    presenter.SetRowSelected("2", false);

    control.chain = renameRule(L"a", L"c");
    presenter.SetControlState(control);

    const std::vector<Row> order = presenter.ExecutionOrder();
    CHECK_EQ(order.size(), std::size_t(1));
    CHECK_EQ(order[0].id.compare("1"), 0);
}

TEST_CASE(PresenterStaleePlanIsRejected)
{
    // A slow worker must not overwrite a newer plan (design §4.2).
    Presenter presenter;
    ControlState control;
    control.chain = renameRule(L"a", L"b");
    presenter.SetControlState(control);
    presenter.SetItems({item("1", L"a.txt")});

    const std::uint64_t oldId = presenter.Plan().planId;
    const std::uint64_t newId = presenter.Rebuild();
    CHECK(newId != oldId);

    PreviewPlan stale;
    stale.planId = oldId;
    CHECK(!presenter.AcceptPlan(oldId, stale));
    CHECK(presenter.AcceptPlan(newId, presenter.Plan()));
}

TEST_CASE(PresenterBlocksOnConflict)
{
    Presenter presenter;
    ControlState control;
    // Both rows collapse to exactly "same.txt": the batch-level check must refuse.
    Rule rule;
    rule.scope = Scope::Name;
    rule.search = L".*";
    rule.replace = L"same";
    rule.match = MatchType::Regex;
    rule.occurrence = Occurrence::All;
    control.chain.rules.push_back(rule);
    presenter.SetControlState(control);
    presenter.SetItems({item("1", L"one.txt"), item("2", L"two.txt")});

    CHECK(!presenter.CanExecute());
    CHECK(presenter.BlockingReason() == std::wstring(L"errors"));
}

TEST_CASE(PresenterExecutionOrderIsDeepestFirst)
{
    // RN-T2-05: a child must be submitted before its parent, or the parent's
    // rename invalidates the child's recorded path.
    Presenter presenter;
    ControlState control;
    control.chain = renameRule(L"x", L"y");
    presenter.SetControlState(control);
    presenter.SetItems({
        item("root", L"xroot", 0, true),
        item("child", L"xchild", 1, true),
        item("grandchild", L"xfile.txt", 2),
    });

    const std::vector<Row> order = presenter.ExecutionOrder();
    CHECK_EQ(order.size(), std::size_t(3));
    CHECK_EQ(order[0].depth, 2u);
    CHECK_EQ(order[1].depth, 1u);
    CHECK_EQ(order[2].depth, 0u);
}

TEST_CASE(PresenterTextModeBuildsFromBuffer)
{
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());

    // The generated buffer is "y 1 one.txt\ny 2 two.txt"; edit the first leaf.
    presenter.SetBuffer(L"y 1 uno.txt\ny 2 two.txt\n");

    const Counts counts = presenter.CurrentCounts();
    CHECK_EQ(counts.ready, 1);
    CHECK_EQ(counts.unchanged, 1);
    CHECK(presenter.CanExecute());
}

TEST_CASE(PresenterTextModeDStateMarksPendingDelete)
{
    // "d" is how the buffer asks for a delete; the first file can still be renamed.
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());
    presenter.SetBuffer(L"y 1 uno.txt\nd 2 two.txt");

    CHECK(presenter.CanExecute());
    CHECK(!presenter.Plan().HasBlockingError());
    CHECK_EQ(presenter.CurrentCounts().ready, 1);
    CHECK_EQ(presenter.CurrentCounts().toDelete, 1);

    const std::vector<Row> order = presenter.ExecutionOrder();
    CHECK_EQ(order.size(), std::size_t(2));
    CHECK(order[1].pendingDelete);
}

TEST_CASE(PresenterTextModeRejectsARemovedLine)
{
    // Removing a line is no longer a delete gesture: it blocks, because an
    // accidental removal must never delete a file.
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());
    presenter.SetBuffer(L"y 1 uno.txt");

    CHECK(!presenter.CanExecute());
    CHECK(presenter.Plan().HasBlockingError());
    CHECK_EQ(presenter.CurrentCounts().toDelete, 0);
}

TEST_CASE(PresenterTextModeNStateUnselectsRow)
{
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());
    presenter.SetBuffer(L"n 1 one.txt\ny 2 two.txt");

    const std::vector<Row> rows = presenter.VisibleRows();
    CHECK_EQ(rows.size(), std::size_t(2));
    CHECK(!rows[0].selected);
    CHECK(rows[1].selected);
}

TEST_CASE(UncheckedRowsDoNotCountTowardsTheBatch)
{
    // The badge, the confirmation and the executor must all agree: an unchecked
    // row is not part of the batch, so it cannot inflate "N items to rename".
    Presenter presenter;
    ControlState state;
    state.chain = renameRule(L"one", L"1st");
    presenter.SetControlState(state);
    presenter.SetItems({item("1", L"one.txt"), item("2", L"two.txt")});

    CHECK_EQ(presenter.CurrentCounts().ready, 1);
    CHECK(presenter.CanExecute());

    presenter.SetRowSelected("2", false);
    CHECK_EQ(presenter.CurrentCounts().ready, 1);
    CHECK_EQ(presenter.ExecutionOrder().size(), std::size_t(1));
}

TEST_CASE(AnUncheckedErrorDoesNotBlockTheBatch)
{
    // Row 2 is unchecked *and* carries an invalid name: it is outside the batch,
    // so it must not stop row 1 from being renamed.
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());
    presenter.SetBuffer(L"y 1 renamed.txt\nn 2 bad?.txt");

    const Counts counts = presenter.CurrentCounts();
    CHECK_EQ(counts.ready, 1);
    CHECK_EQ(counts.errors, 0);
    CHECK(presenter.CanExecute());
    CHECK_EQ(presenter.ExecutionOrder().size(), std::size_t(1));

    // Checking it brings the error back into the batch, and it blocks again.
    presenter.SetRowSelected("2", true);
    CHECK_EQ(presenter.CurrentCounts().errors, 1);
    CHECK(!presenter.CanExecute());
}

TEST_CASE(PresenterRestoresPendingDeleteInOilOrder)
{
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());
    presenter.SetBuffer(L"y 1 renamed-one.txt\nd 2 two.txt");

    CHECK(presenter.RestorePendingDelete("2"));
    CHECK_EQ(presenter.Buffer(), std::wstring(L"y 1 renamed-one.txt\ny 2 two.txt"));
    CHECK_EQ(presenter.CurrentCounts().toDelete, 0);
}

TEST_CASE(PresenterRestoreKeepsUnselectedState)
{
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());
    presenter.SetBuffer(L"n 1 one.txt\nd 2 two.txt");
    CHECK(presenter.RestorePendingDelete("2"));
    CHECK_EQ(presenter.Buffer(), std::wstring(L"n 1 one.txt\ny 2 two.txt"));
}

TEST_CASE(PresenterTextModeRefusesUnauthoredCreate)
{
    // RN-FR-19: a line with no anchor is a create, out of scope for release one.
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());
    presenter.SetBuffer(L"y 1 one.txt\ny 2 two.txt\nbrand-new.txt\n");

    CHECK(!presenter.CanExecute());
}

TEST_CASE(PresenterModesAgreeOnIllegalName)
{
    // RN-T2-06: the same illegal name must be rejected identically in both modes.
    Presenter controlPresenter;
    ControlState control;
    control.chain = renameRule(L"orig", L"bad?");
    controlPresenter.SetControlState(control);
    controlPresenter.SetItems({item("1", L"orig.txt")});

    Presenter textPresenter;
    textPresenter.SetMode(Mode::Text);
    std::vector<OilEntry> table = oilTable();
    table.resize(1);
    table[0].id = 1;
    table[0].originalLeaf = L"orig.txt";
    textPresenter.SetOilTable(table);
    textPresenter.SetBuffer(L"y 1 bad?.txt\n");

    CHECK(!controlPresenter.CanExecute());
    CHECK(!textPresenter.CanExecute());
    CHECK_EQ(controlPresenter.Plan().items[0].status, Status::ErrorInvalidCharacter);
    CHECK_EQ(textPresenter.Plan().items[0].status, Status::ErrorInvalidCharacter);
}

TEST_CASE(PresenterSwitchModeBlockedByUncommittedEdits)
{
    Presenter presenter;
    ControlState control;
    control.chain = renameRule(L"a", L"b");
    presenter.SetControlState(control);
    presenter.SetItems({item("1", L"a.txt")});

    // A ready plan counts as uncommitted, so switching warns.
    CHECK(presenter.HasUncommittedEdits());
    CHECK(!presenter.CanSwitchMode(Mode::Text));

    // Same mode is always allowed.
    CHECK(presenter.CanSwitchMode(Mode::Control));
}

TEST_CASE(PresenterBufferResetsAfterExecution)
{
    // RN-T3-05 (partial): after a batch the buffer must reflect the new names, or
    // a second edit addresses objects that no longer have those names.
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());
    presenter.SetBuffer(L"y 1 uno.txt\ny 2 two.txt\n");

    presenter.ApplyExecutedResults({{"1", L"uno.txt"}}, {});

    CHECK(presenter.Buffer().find(L"uno.txt") != std::wstring::npos);
    CHECK(presenter.Buffer().find(L"one.txt") == std::wstring::npos);
    CHECK_EQ(presenter.CurrentPhase(), Phase::Idle);
}

TEST_CASE(PresenterOilClipboardRoundTrips)
{
    // Copying the buffer out and pasting it back must be a no-op, otherwise a user
    // who pastes into another editor loses the anchors.
    Presenter presenter;
    presenter.SetMode(Mode::Text);
    presenter.SetOilTable(oilTable());

    const std::wstring payload = presenter.OilClipboardPayload();
    presenter.SetBuffer(payload);
    CHECK_EQ(presenter.Plan().items.size(), std::size_t(2));
    CHECK_EQ(presenter.CurrentCounts().unchanged, 2);
}

TEST_CASE(PresenterShowsTheFolderOnlyForAFolderSpanningBatch)
{
    // A keyword search hands over files from several folders: two rows named
    // the same would otherwise look like one row twice. One-folder batches keep
    // the clean leaf-only column (the path bar already says where they are).
    Presenter presenter;
    ControlState control;
    control.chain = renameRule(L"a", L"b");
    presenter.SetControlState(control);

    std::vector<ItemSnapshot> spanning = {item("1", L"a.txt"), item("2", L"a.txt")};
    spanning[0].parent = L"D:\\A";
    spanning[1].parent = L"D:\\B";
    presenter.SetItems(spanning);

    const std::vector<Row> rows = presenter.VisibleRows();
    CHECK_EQ(rows.size(), std::size_t(2));
    CHECK_EQ(rows[0].parent, std::wstring(L"D:\\A"));
    CHECK_EQ(rows[1].parent, std::wstring(L"D:\\B"));

    std::vector<ItemSnapshot> oneFolder = {item("1", L"a.txt"), item("2", L"b.txt")};
    oneFolder[0].parent = L"D:\\A";
    oneFolder[1].parent = L"D:\\A";
    presenter.SetItems(oneFolder);
    for (const Row& row : presenter.VisibleRows())
        CHECK(row.parent.empty());
}

TEST_CASE(PresenterExtensionlessFileNote)
{
    // The "no extension; extension rule not applied" explanation is design §3.5
    // and must surface rather than being swallowed.
    Presenter presenter;
    ControlState control;
    Rule rule;
    rule.scope = Scope::Extension;
    rule.search = L".*";
    rule.replace = L"txt";
    rule.match = MatchType::Regex;
    rule.occurrence = Occurrence::All;
    control.chain.rules.push_back(rule);
    presenter.SetControlState(control);
    presenter.SetItems({item("1", L"README")});

    CHECK_EQ(presenter.Plan().items[0].status, Status::Unchanged);
    CHECK(presenter.CanExecute() == false);
}
