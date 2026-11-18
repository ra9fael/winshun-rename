// presenter.h — the view-model for both modes (M3/M4, design §5).
//
// This is deliberately plain C++ with no Qt: it is where the actual UI behaviour
// is tested, and it is where the two modes are kept honest against each other.
// A Qt/QML layer, when Qt is available, only renders what this exposes and
// forwards input to it. That split is why the project is testable on a machine
// with no Qt installed at all.
//
// The state machine is small on purpose:
//
//   Idle -> Previewing -> Ready -> Executing -> Done
//
// `planId` is the staleness token. Any edit bumps a generation counter; a preview
// built for an older generation is discarded on arrival, which is what makes
// "typing while it computes" safe (design §4.2).
#pragma once

#include "core/rename_core.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rename_ui {

enum class Mode { Control, Text };

enum class Phase {
    Idle,          // no input set
    Previewing,    // a plan is being computed
    Ready,         // a plan is current and can execute
    Executing,
    Done,
};

// One row as the table renders it. Mirrors PlanItem plus the display-only bits
// the UI needs (a stable key for QML delegates, and the notes string).
struct Row {
    std::string id;
    std::wstring originalLeaf;
    std::wstring targetLeaf;
    // Set only when the batch spans folders (a search-driven selection): the
    // folder this row names, so the table can tell two same-named files apart.
    std::wstring parent;
    rename_core::Status status = rename_core::Status::Init;
    rename_core::Action action = rename_core::Action::Rename;
    bool selected = true;
    bool isFolder = false;
    unsigned depth = 0;
    // The item's line is gone from the buffer, or the list marked it: the batch
    // will delete it (to the Recycle Bin) rather than rename it.
    bool pendingDelete = false;
    std::wstring note;          // "trailing whitespace removed", "no extension; ..."
};

struct Counts {
    int ready = 0;
    int unchanged = 0;
    int errors = 0;
    int toDelete = 0;
};

// A filter is a visibility rule, never an input-set change (RN-FR-10).
enum class Filter { All, Selected, Applicable, ToRename };

// Rule chain + flags, the control-mode input.
struct ControlState {
    rename_core::RuleChain chain;
    std::uint32_t flags = 0;
};

// The presenter owns no filesystem handles and never writes. It is fed snapshots
// and asked for plans; execution is the caller's job (rename_win::Execute).
class Presenter {
public:
    struct PendingDeleteState
    {
        std::wstring targetLeaf;
        bool selected = true;
    };

    void SetMode(Mode mode);
    Mode CurrentMode() const { return m_mode; }

    // Control-mode input.
    void SetControlState(ControlState state);
    const ControlState& ControlStateRef() const { return m_control; }

    // The full input set, replaced whenever the selection changes.
    void SetItems(std::vector<rename_core::ItemSnapshot> items);
    // Folder navigation creates a new input set whose short numeric ids may be
    // reused. Do not carry checkbox state from the old folder by coincidence.
    void ReplaceItems(std::vector<rename_core::ItemSnapshot> items);
    const std::vector<rename_core::ItemSnapshot>& Items() const { return m_items; }

    // Logical (OLE) clipboard representation, used by the tests to prove that
    // copy and paste round-trips the oil buffer byte for byte.
    std::wstring OilClipboardPayload() const;

    // Text-mode input: the editable buffer. Setting it invalidates the plan.
    void SetBuffer(std::wstring buffer);
    const std::wstring& Buffer() const { return m_buffer; }
    void SetOilTable(std::vector<rename_core::OilEntry> table);

    // Recompute the plan. Returns the new planId, which is also what a late
    // worker result must match to be accepted.
    std::uint64_t Rebuild();
    const rename_core::PreviewPlan& Plan() const { return m_plan; }
    Phase CurrentPhase() const { return m_phase; }

    // Accept a worker result only if it is still current (design §4.2).
    bool AcceptPlan(std::uint64_t planId, rename_core::PreviewPlan plan);

    // Selection and filtering.
    void SetRowSelected(const std::string& id, bool selected);
    bool RestorePendingDelete(const std::string& id);
    // Undo one row's buffer edit: its name goes back to the on-disk name and its
    // "d" mark is cleared. Cheaper and more predictable than vim's own undo, which
    // any model-level rewrite of the buffer invalidates.
    bool RevertRow(const std::string& id);
    // Undo every name/delete edit in the buffer, keeping the selection.
    bool RevertAllEdits();
    void SelectAllVisible(bool selected);
    void SetFilter(Filter filter);
    Filter CurrentFilter() const { return m_filter; }
    std::vector<Row> VisibleRows() const;
    Counts CurrentCounts() const;

    // Can the batch be submitted? Equivalent to plan.HasBlockingError() plus a
    // non-empty ready count, but expressed as one predicate so the UI cannot
    // forget half the rule.
    bool CanExecute() const;
    // Why CanExecute() is false, as a stable identifier ("no-files", "errors",
    // "nothing-to-rename") the UI maps to the user's language; empty when
    // execution may proceed.
    std::wstring BlockingReason() const;
    // Mode switching preserves the selection but discards the other mode's
    // uncommitted edit, so it reports what would be lost (RN-FR-20).
    bool HasUncommittedEdits() const;
    bool CanSwitchMode(Mode target) const;

    // Which rows would actually be submitted, deepest-first, for the executor.
    std::vector<Row> ExecutionOrder() const;

    // After a successful batch the buffer is rebuilt with fresh ids so a second
    // edit cannot point at the pre-rename names (design §3.8).
    void ApplyExecutedResults(const std::vector<std::pair<std::string, std::wstring>>& renamed,
                              const std::vector<std::string>& deleted);

private:
    void RefreshRows();

    Mode m_mode = Mode::Control;
    Phase m_phase = Phase::Idle;
    ControlState m_control;
    std::vector<rename_core::ItemSnapshot> m_items;
    std::wstring m_buffer;
    std::vector<rename_core::OilEntry> m_oilTable;
    rename_core::PreviewPlan m_plan;
    Filter m_filter = Filter::All;
    std::vector<Row> m_rows;
    std::unordered_map<std::string, PendingDeleteState> m_pendingDeleteStates;
    std::uint64_t m_generation = 0;
};

// Build the buffer text from an id table. Shared by the presenter and the tests
// so the two cannot drift on formatting.
std::wstring FormatOilBuffer(const std::vector<rename_core::OilEntry>& table,
                             const std::vector<rename_core::PlanItem>* plan = nullptr);

} // namespace rename_ui
