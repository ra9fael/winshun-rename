// presenter.cpp — the view-model shared by both modes (M3/M4, design §5).
//
// The interesting decisions:
//
//   * `Rebuild()` bumps a generation and mints a plan id together. A worker
//     result carrying an older id is rejected by `AcceptPlan`, which is the whole
//     defence against a slow preview overwriting a fast one.
//   * Filters never touch the input set; they only decide which rows the table
//     returns. `ExecutionOrder()` works from the plan, not from the visible rows,
//     so a filter can never silently drop an item from the batch.
//   * Mode switching keeps the selection and reports uncommitted edits instead of
//     discarding them silently.
#include "ui/presenter.h"

#include <algorithm>
#include <cwctype>
#include <string>

namespace rename_ui {

namespace {

std::wstring NoteFor(const rename_core::PlanItem& item, bool skippedExtension)
{
    // Stable identifiers separated by spaces, not prose: the QML layer maps each
    // to the user's language (Main.qml), and the tests assert on the identifiers
    // without caring about wording.
    std::wstring note;
    if (skippedExtension)
        note += L"no-extension ";
    if (item.trimmed)
        note += L"trimmed ";
    if (item.hadNbsp)
        note += L"nbsp ";
    return note;
}

bool IsError(rename_core::Status status)
{
    switch (status)
    {
    case rename_core::Status::ErrorInvalidCharacter:
    case rename_core::Status::ErrorTooLong:
    case rename_core::Status::ErrorAlreadyExists:
    case rename_core::Status::ErrorEmptyName:
    case rename_core::Status::ErrorBadRule:
        return true;
    default:
        return false;
    }
}

// The first column of a buffer line: "y" rename, "n" skip, "d" delete. Both
// states are visible, which a leading space would not be.
wchar_t StateLetter(bool selected, bool deleted)
{
    if (deleted)
        return L'd';
    return selected ? L'y' : L'n';
}

} // namespace

std::wstring FormatOilBuffer(const std::vector<rename_core::OilEntry>& table,
                             const std::vector<rename_core::PlanItem>* plan)
{
    std::wstring buffer;
    for (const rename_core::OilEntry& entry : table)
    {
        bool selected = true;
        bool deleted = false;
        std::wstring leaf = entry.originalLeaf;
        if (plan)
        {
            for (const rename_core::PlanItem& item : *plan)
            {
                if (item.id == std::to_string(entry.id))
                {
                    deleted = item.action == rename_core::Action::Delete;
                    selected = item.selected;
                    leaf = item.targetLeaf;
                    break;
                }
            }
        }
        if (!buffer.empty())
            buffer += L"\n";
        buffer += StateLetter(selected, deleted);
        buffer += L' ';
        buffer += std::to_wstring(entry.id);
        buffer += L' ';
        buffer += leaf;
        // No trailing newline: QPlainTextEdit otherwise creates a non-item
        // final block which the cursor can enter below the last file.
    }
    return buffer;
}

std::wstring FormatOilBufferWithPending(
    const std::vector<rename_core::OilEntry>& table,
    const std::vector<rename_core::PlanItem>& plan,
    const std::unordered_map<std::string, Presenter::PendingDeleteState>& pending)
{
    std::wstring buffer;
    for (const rename_core::OilEntry& entry : table)
    {
        const std::string id = std::to_string(entry.id);
        const auto planned = std::find_if(plan.begin(), plan.end(),
            [&](const rename_core::PlanItem& item) { return item.id == id; });
        if (!buffer.empty())
            buffer += L"\n";
        const auto saved = pending.find(id);
        const bool deleted = planned != plan.end() && planned->action == rename_core::Action::Delete;
        const bool selected = saved != pending.end() ? saved->second.selected
                            : planned == plan.end() ? true : planned->selected;
        const std::wstring& leaf = saved != pending.end() ? saved->second.targetLeaf
                                 : planned == plan.end() ? entry.originalLeaf : planned->targetLeaf;
        buffer += StateLetter(selected, deleted);
        buffer += L' ';
        buffer += std::to_wstring(entry.id);
        buffer += L' ';
        buffer += leaf;
    }
    return buffer;
}

void Presenter::SetMode(Mode mode)
{
    m_mode = mode;
    RefreshRows();
}

void Presenter::SetControlState(ControlState state)
{
    m_control = std::move(state);
    Rebuild();
}

void Presenter::SetItems(std::vector<rename_core::ItemSnapshot> items)
{
    m_items = std::move(items);
    m_phase = m_items.empty() ? Phase::Idle : Phase::Ready;
    Rebuild();
}

void Presenter::ReplaceItems(std::vector<rename_core::ItemSnapshot> items)
{
    m_rows.clear();
    m_plan = rename_core::PreviewPlan{};
    SetItems(std::move(items));
}

void Presenter::SetBuffer(std::wstring buffer)
{
    const std::vector<rename_core::PlanItem> previousPlan = m_plan.items;
    m_buffer = std::move(buffer);
    Rebuild();
    // Cache the last visible line the first time it becomes absent. That makes a
    // planned delete reversible without losing a user-edited target name or its
    // / versus // participation marker.
    for (const rename_core::PlanItem& item : m_plan.items)
    {
        if (item.action != rename_core::Action::Delete)
            continue;
        if (m_pendingDeleteStates.contains(item.id))
            continue;
        const auto previous = std::find_if(previousPlan.begin(), previousPlan.end(),
            [&](const rename_core::PlanItem& candidate) { return candidate.id == item.id; });
        m_pendingDeleteStates.emplace(item.id, previous == previousPlan.end()
            ? PendingDeleteState{item.targetLeaf, item.selected}
            : PendingDeleteState{previous->targetLeaf, previous->selected});
    }
}

void Presenter::SetOilTable(std::vector<rename_core::OilEntry> table)
{
    m_oilTable = std::move(table);
    if (m_buffer.empty())
        m_buffer = FormatOilBuffer(m_oilTable);
    Rebuild();
}

std::wstring Presenter::OilClipboardPayload() const
{
    // The buffer is exactly what a user would copy out of the editor, so the
    // clipboard payload *is* the buffer. Kept as a named function so a future
    // "copy only selected rows" change has one place to live.
    return m_buffer;
}

std::uint64_t Presenter::Rebuild()
{
    ++m_generation;
    const std::uint64_t planId = rename_core::NextPlanId();

    // Control mode has no text representation for selection, so preserve the
    // list state across rule recalculation. In text mode / versus // is
    // authoritative and must be allowed to change selection.
    std::vector<std::pair<std::string, bool>> previousSelection;
    previousSelection.reserve(m_rows.size());
    if (m_mode == Mode::Control)
    {
        for (const Row& row : m_rows)
            previousSelection.emplace_back(row.id, row.selected);
    }

    if (m_mode == Mode::Control)
    {
        m_plan = rename_core::BuildControlPlan(m_items, m_control.chain, m_control.flags, planId);
    }
    else
    {
        m_plan = rename_core::BuildOilPlan(m_buffer, m_oilTable, m_control.flags, planId);
    }

    for (rename_core::PlanItem& item : m_plan.items)
    {
        for (const auto& [id, selected] : previousSelection)
        {
            if (id == item.id)
            {
                item.selected = selected;
                break;
            }
        }
    }

    m_phase = m_plan.items.empty() ? Phase::Idle : Phase::Ready;
    RefreshRows();
    return planId;
}

bool Presenter::AcceptPlan(std::uint64_t planId, rename_core::PreviewPlan plan)
{
    // A result for an older generation is simply dropped: the user has typed
    // since, and showing a stale target name is worse than showing nothing.
    if (planId != m_plan.planId)
        return false;

    m_plan = std::move(plan);
    m_phase = m_plan.items.empty() ? Phase::Idle : Phase::Ready;
    RefreshRows();
    return true;
}

void Presenter::RefreshRows()
{
    m_rows.clear();
    m_rows.reserve(m_plan.items.size());

    // One-folder batches keep the leaf-only column; a batch that spans folders
    // (a keyword search, or a multi-select) puts each row's folder on it, or
    // two files called "报告.txt" would look like the same row twice.
    std::vector<std::wstring> folders;
    for (const rename_core::PlanItem& item : m_plan.items)
    {
        const auto known = std::find_if(folders.begin(), folders.end(),
            [&](const std::wstring& folder) { return rename_core::NfcEqual(folder, item.parent); });
        if (known == folders.end())
            folders.push_back(item.parent);
    }
    const bool showParent = folders.size() > 1;

    for (const rename_core::PlanItem& item : m_plan.items)
    {
        Row row;
        row.id = item.id;
        row.originalLeaf = item.originalLeaf;
        row.targetLeaf = item.targetLeaf;
        row.parent = showParent ? item.parent : std::wstring();
        row.status = item.status;
        row.action = item.action;
        row.selected = item.selected;
        row.isFolder = item.isFolder;
        row.depth = item.depth;
        row.pendingDelete = item.action == rename_core::Action::Delete;
        m_rows.push_back(std::move(row));
    }
}

void Presenter::SetRowSelected(const std::string& id, bool selected)
{
    for (Row& row : m_rows)
    {
        if (row.id == id)
            row.selected = selected;
    }
    for (rename_core::PlanItem& item : m_plan.items)
    {
        if (item.id == id)
            item.selected = selected;
    }
    if (m_mode == Mode::Text)
        m_buffer = FormatOilBuffer(m_oilTable, &m_plan.items);
    RefreshRows();
}

bool Presenter::RestorePendingDelete(const std::string& id)
{
    if (m_mode != Mode::Text)
        return false;
    const auto item = std::find_if(m_plan.items.begin(), m_plan.items.end(),
        [&](const rename_core::PlanItem& candidate) { return candidate.id == id; });
    if (item == m_plan.items.end() || item->action != rename_core::Action::Delete)
        return false;

    const auto saved = m_pendingDeleteStates.find(id);
    const PendingDeleteState restored = saved == m_pendingDeleteStates.end()
        ? PendingDeleteState{item->targetLeaf, item->selected}
        : saved->second;
    m_pendingDeleteStates.erase(id);
    std::wstring buffer;
    for (const rename_core::OilEntry& entry : m_oilTable)
    {
        const std::string entryId = std::to_string(entry.id);
        const auto planned = std::find_if(m_plan.items.begin(), m_plan.items.end(),
            [&](const rename_core::PlanItem& candidate) { return candidate.id == entryId; });
        const bool restoreThis = entryId == id;
        if (!buffer.empty())
            buffer += L"\n";
        const bool deleted = !restoreThis && planned != m_plan.items.end()
                             && planned->action == rename_core::Action::Delete;
        const bool selected = restoreThis ? restored.selected
                            : planned == m_plan.items.end() ? true : planned->selected;
        const std::wstring& leaf = restoreThis ? restored.targetLeaf
                                 : planned == m_plan.items.end() ? entry.originalLeaf : planned->targetLeaf;
        buffer += StateLetter(selected, deleted);
        buffer += L' ';
        buffer += std::to_wstring(entry.id);
        buffer += L' ';
        buffer += leaf;
    }
    m_buffer = std::move(buffer);
    Rebuild();
    return true;
}

bool Presenter::RevertRow(const std::string& id)
{
    if (m_mode != Mode::Text)
        return false;

    bool found = false;
    std::wstring buffer;
    for (const rename_core::OilEntry& entry : m_oilTable)
    {
        const std::string entryId = std::to_string(entry.id);
        const auto planned = std::find_if(m_plan.items.begin(), m_plan.items.end(),
            [&](const rename_core::PlanItem& candidate) { return candidate.id == entryId; });
        const bool revertThis = entryId == id;
        found = found || revertThis;
        if (!buffer.empty())
            buffer += L"\n";
        const bool deleted = !revertThis && planned != m_plan.items.end()
                             && planned->action == rename_core::Action::Delete;
        // The reverted row goes back to its on-disk name and to whatever
        // selection it had before the edit; every other row keeps its state.
        const bool selected = revertThis ? true
                            : planned == m_plan.items.end() ? true : planned->selected;
        const std::wstring& leaf = revertThis ? entry.originalLeaf
                                 : planned == m_plan.items.end() ? entry.originalLeaf : planned->targetLeaf;
        buffer += StateLetter(selected, deleted);
        buffer += L' ';
        buffer += std::to_wstring(entry.id);
        buffer += L' ';
        buffer += leaf;
    }
    if (!found)
        return false;
    m_pendingDeleteStates.erase(id);
    m_buffer = std::move(buffer);
    Rebuild();
    return true;
}

bool Presenter::RevertAllEdits()
{
    if (m_mode != Mode::Text)
        return false;

    // Names and delete marks go back to the on-disk state; the selection the user
    // set is kept, because unchecking a row is not an edit of the batch's names.
    std::wstring buffer;
    for (const rename_core::OilEntry& entry : m_oilTable)
    {
        const std::string entryId = std::to_string(entry.id);
        const auto planned = std::find_if(m_plan.items.begin(), m_plan.items.end(),
            [&](const rename_core::PlanItem& candidate) { return candidate.id == entryId; });
        const bool selected = planned == m_plan.items.end() ? true : planned->selected;
        if (!buffer.empty())
            buffer += L"\n";
        buffer += StateLetter(selected, false);
        buffer += L' ';
        buffer += std::to_wstring(entry.id);
        buffer += L' ';
        buffer += entry.originalLeaf;
    }
    m_pendingDeleteStates.clear();
    m_buffer = std::move(buffer);
    Rebuild();
    return true;
}

void Presenter::SelectAllVisible(bool selected)
{
    for (const Row& visible : VisibleRows())
        SetRowSelected(visible.id, selected);
}

void Presenter::SetFilter(Filter filter)
{
    m_filter = filter;
}

std::vector<Row> Presenter::VisibleRows() const
{
    std::vector<Row> out;
    for (const Row& row : m_rows)
    {
        const bool applicable = !IsError(row.status) &&
                                (row.status != rename_core::Status::Unchanged || row.pendingDelete);

        switch (m_filter)
        {
        case Filter::All:
            out.push_back(row);
            break;
        case Filter::Selected:
            if (row.selected)
                out.push_back(row);
            break;
        case Filter::Applicable:
            if (applicable)
                out.push_back(row);
            break;
        case Filter::ToRename:
            if (row.selected && row.status == rename_core::Status::Ready)
                out.push_back(row);
            break;
        }
    }
    return out;
}

Counts Presenter::CurrentCounts() const
{
    // Only checked rows are in the batch: the executor, the badges and the
    // confirmation must all agree, so an unchecked row never inflates them.
    Counts counts;
    for (const Row& row : m_rows)
    {
        if (!row.selected)
            continue;
        if (IsError(row.status))
            ++counts.errors;
        else if (row.pendingDelete)
            ++counts.toDelete;
        else if (row.status == rename_core::Status::Ready)
            ++counts.ready;
        else
            ++counts.unchanged;
    }
    return counts;
}

bool Presenter::CanExecute() const
{
    if (m_phase != Phase::Ready)
        return false;
    if (m_plan.HasBlockingError())
        return false;

    const Counts counts = CurrentCounts();
    return counts.ready > 0 || counts.toDelete > 0;
}

std::wstring Presenter::BlockingReason() const
{
    // Stable identifiers, not prose: the QML layer maps them to the user's
    // language (Main.qml), and the tests assert on them without caring about
    // wording. A new reason is one enum-like string here plus one mapping there.
    if (m_phase == Phase::Idle)
        return L"no-files";

    const Counts counts = CurrentCounts();
    if (counts.errors > 0)
        return L"errors";
    if (counts.ready == 0)
        return L"nothing-to-rename";
    return std::wstring();
}

bool Presenter::HasUncommittedEdits() const
{
    if (m_mode == Mode::Text)
        return m_buffer != FormatOilBuffer(m_oilTable);

    // Control mode has no "dirty" concept beyond the plan itself; an unexecuted
    // Ready plan counts as uncommitted so switching modes warns.
    return m_phase == Phase::Ready && CurrentCounts().ready > 0;
}

bool Presenter::CanSwitchMode(Mode target) const
{
    if (target == m_mode)
        return true;
    return !HasUncommittedEdits();
}

std::vector<Row> Presenter::ExecutionOrder() const
{
    // Deepest first, then by id so the order is deterministic for tests. Built
    // from the plan rather than from visible rows: a filter must never shrink the
    // batch (RN-FR-10).
    //
    // Two constraints have to hold at once:
    //   * a child before its parent, or the parent's rename invalidates the
    //     child's path;
    //   * a row that frees a name before the row that takes it, or the executor
    //     finds the target occupied (the plan records that in waitsForId).
    // A row waiting for another must come after it; depth is the tie-break. The
    // plan already refused cycles, so this terminates.
    std::vector<Row> candidates;
    for (const Row& row : m_rows)
    {
        if (!row.selected || row.status != rename_core::Status::Ready)
            continue;
        candidates.push_back(row);
    }

    // waitsForId lives in the plan, which is the same id space as the rows.
    auto waitsFor = [this](const std::string& id) -> std::string {
        for (const rename_core::PlanItem& item : m_plan.items)
        {
            if (item.id == id)
                return item.waitsForId;
        }
        return std::string();
    };

    std::vector<Row> out;
    std::vector<bool> taken(candidates.size(), false);
    std::size_t emitted = 0;
    while (emitted < candidates.size())
    {
        // Pick the deepest candidate whose dependency is already emitted (or has
        // no dependency at all, or whose dependency is not in this batch).
        std::size_t pick = candidates.size();
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (taken[i])
                continue;

            const std::string wait = waitsFor(candidates[i].id);
            bool ready = true;
            if (!wait.empty())
            {
                bool inBatch = false;
                bool emittedYet = false;
                for (std::size_t k = 0; k < candidates.size(); ++k)
                {
                    if (candidates[k].id != wait)
                        continue;
                    inBatch = true;
                    emittedYet = taken[k];
                    break;
                }
                ready = !inBatch || emittedYet;
            }
            if (!ready)
                continue;

            if (pick == candidates.size())
            {
                pick = i;
                continue;
            }
            if (candidates[i].depth > candidates[pick].depth
                || (candidates[i].depth == candidates[pick].depth
                    && candidates[i].id < candidates[pick].id))
                pick = i;
        }
        if (pick == candidates.size())
            break;   // cannot happen with a cycle-free plan; stop rather than spin
        taken[pick] = true;
        out.push_back(candidates[pick]);
        ++emitted;
    }

    return out;
}

void Presenter::ApplyExecutedResults(const std::vector<std::pair<std::string, std::wstring>>& renamed,
                                     const std::vector<std::string>& deleted)
{
    // Rebuild the oil table from the results: the renamed entry now has the new
    // leaf as its original, and ids stay put so a second edit addresses the same
    // objects. Without this, a second edit would point at the pre-rename names.
    for (const auto& [id, newLeaf] : renamed)
    {
        for (rename_core::OilEntry& entry : m_oilTable)
        {
            if (std::to_string(entry.id) == id)
            {
                entry.originalLeaf = newLeaf;
                break;
            }
        }
    }
    for (const std::string& id : deleted)
    {
        m_oilTable.erase(std::remove_if(m_oilTable.begin(), m_oilTable.end(),
                                        [&](const rename_core::OilEntry& entry) {
                                            return std::to_string(entry.id) == id;
                                        }),
                         m_oilTable.end());
    }

    // Keep the completed batch visible. Renamed entries now use their on-disk
    // leaves; deleted entries have been removed from the oil table above. A user
    // can therefore inspect and continue working with the refreshed list instead
    // of seeing a completion sentence replace the entire preview.
    for (const auto& [id, newLeaf] : renamed)
    {
        for (rename_core::ItemSnapshot& item : m_items)
        {
            if (item.id == id)
            {
                item.leaf = newLeaf;
                break;
            }
        }
    }
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                 [&](const rename_core::ItemSnapshot& item) {
                                     return std::find(deleted.begin(), deleted.end(), item.id) != deleted.end();
                                 }),
                  m_items.end());
    m_buffer = FormatOilBuffer(m_oilTable);
    Rebuild();
    m_phase = Phase::Idle;
}

} // namespace rename_ui
