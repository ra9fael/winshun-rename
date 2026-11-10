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

} // namespace

std::wstring FormatOilBuffer(const std::vector<rename_core::OilEntry>& table)
{
    std::wstring buffer;
    for (const rename_core::OilEntry& entry : table)
    {
        buffer += L"/";
        buffer += std::to_wstring(entry.id);
        buffer += L" ";
        buffer += entry.originalLeaf;
        if (entry.isFolder)
            buffer += L"/";
        buffer += L"\n";
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

void Presenter::SetBuffer(std::wstring buffer)
{
    m_buffer = std::move(buffer);
    Rebuild();
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

    // Preserve selection *before* rebuilding: RefreshRows() replaces m_rows, so a
    // capture afterwards would read the new (all-selected) state and every
    // unchecked row would come back checked after each keystroke.
    std::vector<std::pair<std::string, bool>> previousSelection;
    previousSelection.reserve(m_rows.size());
    for (const Row& row : m_rows)
        previousSelection.emplace_back(row.id, row.selected);

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

    for (const rename_core::PlanItem& item : m_plan.items)
    {
        Row row;
        row.id = item.id;
        row.originalLeaf = item.originalLeaf;
        row.targetLeaf = item.targetLeaf;
        row.status = item.status;
        row.selected = item.selected;
        row.isFolder = item.isFolder;
        row.depth = item.depth;
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
                                row.status != rename_core::Status::Unchanged;

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
    Counts counts;
    for (const Row& row : m_rows)
    {
        if (IsError(row.status))
            ++counts.errors;
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
    return counts.ready > 0;
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
    std::vector<Row> out;
    for (const Row& row : m_rows)
    {
        if (!row.selected || row.status != rename_core::Status::Ready)
            continue;
        out.push_back(row);
    }

    std::stable_sort(out.begin(), out.end(), [](const Row& a, const Row& b) {
        if (a.depth != b.depth)
            return a.depth > b.depth;
        return a.id < b.id;
    });
    return out;
}

void Presenter::ApplyExecutedResults(const std::vector<std::pair<std::string, std::wstring>>& applied)
{
    // Rebuild the oil table from the results: the renamed entry now has the new
    // leaf as its original, and ids stay put so a second edit addresses the same
    // objects. Without this, a second edit would point at the pre-rename names.
    for (const auto& [id, newLeaf] : applied)
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

    // Drop the rows that succeeded; a batch is finished once executed. The oil
    // table keeps the new names so the buffer shows the state the disk is in,
    // and the empty input set is what the empty table honestly reports.
    m_items.clear();
    m_buffer = FormatOilBuffer(m_oilTable);
    m_plan = rename_core::PreviewPlan{};
    m_rows.clear();
    m_phase = Phase::Idle;
}

} // namespace rename_ui
