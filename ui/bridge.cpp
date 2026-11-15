// bridge.cpp — Qt adapter over rename_ui::Presenter (M4).
//
// Nothing here decides anything: it converts to and from Qt types, pushes edits
// into the presenter, and republishes the result. If a behaviour question ever
// arises while reading this file, the answer belongs in core/ or presenter.cpp.
#include "ui/bridge.h"
#include "win/platform.h"

#include <QStringList>

namespace rename_qml {

namespace {

QString ToQt(const std::wstring& text)
{
    return QString::fromStdWString(text);
}

std::wstring FromQt(const QString& text)
{
    return text.toStdWString();
}

QString StatusText(rename_core::Status status)
{
    return QString::fromLatin1(rename_core::StatusId(status).data());
}

} // namespace

// ---- PreviewModel ------------------------------------------------------------

PreviewModel::PreviewModel(QObject* parent) : QAbstractListModel(parent) {}

int PreviewModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant PreviewModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const rename_ui::Row& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case IdRole:       return QString::fromStdString(row.id);
    case OriginalRole: return ToQt(row.originalLeaf);
    case TargetRole:   return ToQt(row.targetLeaf);
    case StatusRole:   return StatusText(row.status);
    case SelectedRole: return row.selected;
    case IsFolderRole: return row.isFolder;
    case DepthRole:    return static_cast<int>(row.depth);
    case NoteRole:     return ToQt(row.note);
    default:           return {};
    }
}

bool PreviewModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return false;
    if (role != SelectedRole)
        return false;

    rename_ui::Row& row = m_rows[static_cast<std::size_t>(index.row())];
    const bool selected = value.toBool();
    if (row.selected == selected)
        return false;

    row.selected = selected;
    emit dataChanged(index, index, {SelectedRole});
    emit selectionChanged(QString::fromStdString(row.id), selected);
    return true;
}

Qt::ItemFlags PreviewModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    // Only the checkbox is editable; the names are read-only because editing them
    // is what the text mode is for (UI-SPEC.md §2.3).
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

QHash<int, QByteArray> PreviewModel::roleNames() const
{
    return {
        {IdRole, "rowId"},
        {OriginalRole, "originalName"},
        {TargetRole, "targetName"},
        {StatusRole, "status"},
        {SelectedRole, "selected"},
        {IsFolderRole, "isFolder"},
        {DepthRole, "depth"},
        {NoteRole, "note"},
    };
}

void PreviewModel::setRows(const std::vector<rename_ui::Row>& rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

// ---- Bridge ------------------------------------------------------------------

Bridge::Bridge(QObject* parent) : QObject(parent)
{
    connect(&m_previewModel, &PreviewModel::selectionChanged, this,
            [this](const QString& id, bool selected) {
                m_presenter.SetRowSelected(id.toStdString(), selected);
                emit countsChanged();
                emit changed();
            });
}

void Bridge::ApplyAppearance(const QString& locale, bool dark, const QString& uiSize,
                             const QVariantMap& nativeLook)
{
    // Kept verbatim rather than resolved here: a key QML does not know is
    // ignored by design (the host may add fields), and a missing key falls back
    // to the palette in Tokens.qml. Resolving in C++ would mean two sources of
    // truth for the same token set.
    m_locale = locale.isEmpty() ? QStringLiteral("zh") : locale;
    m_dark = dark;
    m_uiSize = uiSize.isEmpty() ? QStringLiteral("standard") : uiSize;
    m_look = nativeLook;
    emit appearanceChanged();
}

QString Bridge::mode() const
{
    return m_presenter.CurrentMode() == rename_ui::Mode::Text ? QStringLiteral("text")
                                                             : QStringLiteral("control");
}

QString Bridge::phase() const
{
    switch (m_presenter.CurrentPhase())
    {
    case rename_ui::Phase::Idle:       return QStringLiteral("idle");
    case rename_ui::Phase::Previewing: return QStringLiteral("previewing");
    case rename_ui::Phase::Ready:      return QStringLiteral("ready");
    case rename_ui::Phase::Executing:  return QStringLiteral("executing");
    case rename_ui::Phase::Done:       return QStringLiteral("done");
    }
    return QStringLiteral("idle");
}

QString Bridge::buffer() const
{
    return ToQt(m_presenter.Buffer());
}

void Bridge::setBuffer(const QString& text)
{
    m_presenter.SetBuffer(FromQt(text));
    PublishRows();
    emit changed();
    emit countsChanged();
}

QString Bridge::blockingReason() const
{
    // A finished batch is not a blocked one: once the run reported what it did,
    // the footer must not also claim there are no files.
    if (!m_doneLine.isEmpty())
        return QString();

    // The presenter reports why, as a stable identifier; this maps it to the
    // user's language. The counts themselves are separate properties, so the
    // wording can include them without the presenter knowing any language.
    const std::wstring reason = m_presenter.BlockingReason();
    if (reason.empty())
        return QString();

    const bool zh = m_locale.startsWith(QLatin1String("zh"));
    const int errors = m_presenter.CurrentCounts().errors;
    if (reason == L"errors")
        return zh ? QStringLiteral("%1 项有问题，不能执行").arg(errors)
                  : QStringLiteral("%1 item(s) with problems; cannot run").arg(errors);
    if (reason == L"nothing-to-rename")
        return zh ? QStringLiteral("没有要改的名字")
                  : QStringLiteral("Nothing to rename");
    if (reason == L"no-files")
        return zh ? QStringLiteral("还没有文件")
                  : QStringLiteral("No files yet");
    return ToQt(reason);
}

int Bridge::readyCount() const { return m_presenter.CurrentCounts().ready; }
int Bridge::unchangedCount() const { return m_presenter.CurrentCounts().unchanged; }
int Bridge::errorCount() const { return m_presenter.CurrentCounts().errors; }
bool Bridge::canExecute() const { return m_presenter.CanExecute(); }
int Bridge::visibleCount() const { return static_cast<int>(m_presenter.VisibleRows().size()); }
int Bridge::totalCount() const { return static_cast<int>(m_presenter.Items().size()); }

bool Bridge::allSelected() const
{
    // The header box is ticked only when every row is: a partial selection has to
    // read as partial, or one click would silently drop rows the user kept.
    const std::vector<rename_ui::Row> rows = m_presenter.VisibleRows();
    if (rows.empty())
        return false;
    for (const rename_ui::Row& row : rows)
    {
        if (!row.selected)
            return false;
    }
    return true;
}

bool Bridge::ruleRegexInvalid() const
{
    // "Regex" is on and the pattern does not compile: the field turns red before
    // the user presses anything, which is the only useful time to say so. A bad
    // pattern surfaces as a bad-rule row, so the plan is what to ask.
    if (m_ruleMatch != 1 || m_ruleFind.isEmpty())
        return false;
    for (const rename_core::PlanItem& item : m_presenter.Plan().items)
    {
        if (item.status == rename_core::Status::ErrorBadRule)
            return true;
    }
    return false;
}

QAbstractListModel* Bridge::previewModel() { return &m_previewModel; }

void Bridge::setSelection(const QStringList& paths, bool recursive)
{
    rename_win::EnumerateRequest request;
    request.recursive = recursive;
    for (const QString& path : paths)
        request.roots.push_back(FromQt(path));

    const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
    m_collected = collected.items;

    std::vector<rename_core::ItemSnapshot> items;
    items.reserve(collected.items.size());
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

    m_presenter.SetItems(std::move(items));
    PublishRows();
    emit changed();
    emit countsChanged();
}

void Bridge::setFolder(const QString& folder, bool recursive)
{
    // `listContents`: the folder is the place to work in, so its direct children
    // are the input set. Same presenter, same plan, same executor as a selection
    // — "here is a folder" and "here are these files" only differ in where the
    // rows come from.
    rename_win::EnumerateRequest request;
    request.recursive = recursive;
    request.listContents = true;
    request.roots.push_back(FromQt(folder));

    const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
    m_collected = collected.items;

    std::vector<rename_core::ItemSnapshot> items;
    items.reserve(collected.items.size());
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

    m_presenter.SetItems(std::move(items));
    m_folder = folder;
    if (collected.blocked)
        m_doneLine = ToQt(collected.blockReason);
    PublishRows();
    emit changed();
    emit countsChanged();
}

void Bridge::switchMode(const QString& target)
{
    const rename_ui::Mode wanted = (target == QStringLiteral("text")) ? rename_ui::Mode::Text
                                                                     : rename_ui::Mode::Control;
    if (!m_presenter.CanSwitchMode(wanted))
        return;   // uncommitted edits: the QML layer shows the confirm dialog
    m_presenter.SetMode(wanted);
    if (wanted == rename_ui::Mode::Text && m_presenter.Buffer().empty())
    {
        // First entry into the text mode: build the buffer from the input set,
        // one "/<id> <name>" line per item, so the user starts from what the
        // host handed over rather than from an empty editor.
        std::vector<rename_core::OilEntry> table;
        table.reserve(m_collected.size());
        for (const rename_win::CollectedItem& entry : m_collected)
        {
            rename_core::OilEntry line;
            line.id = static_cast<long long>(table.size()) + 1;
            line.originalLeaf = entry.leaf;
            line.path = entry.fullPath;
            line.isFolder = entry.isFolder;
            line.depth = entry.depth;
            table.push_back(std::move(line));
        }
        m_presenter.SetOilTable(std::move(table));
    }
    PublishRows();
    emit changed();
}

void Bridge::setFilter(const QString& filter)
{
    if (filter == QStringLiteral("selected"))
        m_presenter.SetFilter(rename_ui::Filter::Selected);
    else if (filter == QStringLiteral("applicable"))
        m_presenter.SetFilter(rename_ui::Filter::Applicable);
    else if (filter == QStringLiteral("to-rename"))
        m_presenter.SetFilter(rename_ui::Filter::ToRename);
    else
        m_presenter.SetFilter(rename_ui::Filter::All);
    PublishRows();
    emit changed();
}

void Bridge::toggleRow(const QString& id, bool selected)
{
    m_presenter.SetRowSelected(id.toStdString(), selected);
    PublishRows();
    emit countsChanged();
    emit changed();
}

void Bridge::selectAll(bool selected)
{
    // The presenter owns the rule that a filter never changes the input set, so
    // this selects the rows currently visible rather than everything (RN-FR-10).
    m_presenter.SelectAllVisible(selected);
    PublishRows();
    emit countsChanged();
    emit changed();
}

void Bridge::setCaseTransform(int which)
{
    // A case transform is a batch-level flag in the core, not a rule: it applies
    // to whatever the rules produced. `which` is the chip pressed, and pressing
    // the same chip again turns it off, which is what a toggle should do.
    const std::uint32_t all = rename_core::FlagUppercase | rename_core::FlagLowercase
                            | rename_core::FlagTitleCase | rename_core::FlagCapitalize;

    rename_ui::ControlState state = m_presenter.ControlStateRef();
    const std::uint32_t previous = state.flags & all;
    state.flags &= ~all;

    std::uint32_t wanted = 0;
    switch (which)
    {
    case 1: wanted = rename_core::FlagLowercase; break;
    case 2: wanted = rename_core::FlagUppercase; break;
    case 3: wanted = rename_core::FlagCapitalize; break;
    case 4: wanted = rename_core::FlagTitleCase; break;
    default: break;
    }
    if (wanted != 0 && previous != wanted)
        state.flags |= wanted;

    m_caseTransform = (wanted != 0 && previous != wanted)
        ? which
        : 0;

    m_presenter.SetControlState(std::move(state));
    PublishRows();
    emit ruleChanged();
    emit changed();
    emit countsChanged();
}

void Bridge::setRule(int scope, int match, const QString& find, const QString& replace,
                     bool enabled, bool matchAll, bool caseSensitive)
{
    m_ruleScope = scope;
    m_ruleMatch = match;
    m_ruleFind = find;
    m_ruleReplace = replace;
    m_ruleEnabled = enabled;
    m_ruleMatchAll = matchAll;
    m_ruleCaseSensitive = caseSensitive;

    rename_ui::ControlState state = m_presenter.ControlStateRef();
    state.chain.rules.clear();

    rename_core::Rule rule;
    rule.enabled = enabled;
    rule.scope = (scope == 1) ? rename_core::Scope::Extension
                              : (scope == 2 ? rename_core::Scope::Full : rename_core::Scope::Name);
    rule.match = (match == 1) ? rename_core::MatchType::Regex : rename_core::MatchType::Literal;
    rule.occurrence = matchAll ? rename_core::Occurrence::All : rename_core::Occurrence::First;
    rule.search = FromQt(find);
    rule.replace = FromQt(replace);
    // An empty find is "no rule", not "match everything": the latter would make
    // a half-typed query rewrite every name.
    if (enabled && !rule.search.empty())
        state.chain.rules.push_back(std::move(rule));

    // Case sensitivity is a flag on the whole batch, as PowerRename's checkbox is
    // (not per rule): one batch compares names one way.
    if (caseSensitive)
        state.flags |= rename_core::FlagCaseSensitive;
    else
        state.flags &= ~static_cast<std::uint32_t>(rename_core::FlagCaseSensitive);

    m_presenter.SetControlState(std::move(state));
    PublishRows();
    emit ruleChanged();
    emit changed();
    emit countsChanged();
}

void Bridge::setScope(int scope)
{
    if (scope == m_ruleScope)
        return;
    m_ruleScope = scope;
    // Reapply with the text the fields hold: the three buttons only change where
    // the rule looks, never what it looks for.
    setRule(m_ruleScope, m_ruleMatch, m_ruleFind, m_ruleReplace, m_ruleEnabled,
            m_ruleMatchAll, m_ruleCaseSensitive);
}

void Bridge::setSingleRule(int scope, int match, const QString& find,
                           const QString& replace, bool enabled)
{
    setRule(scope, match, find, replace, enabled, /*matchAll=*/true, /*caseSensitive=*/false);
}

bool Bridge::apply()
{
    if (!m_presenter.CanExecute())
        return false;

    std::vector<rename_win::ExecuteItem> batch;
    std::vector<std::pair<std::string, std::wstring>> applied;

    for (const rename_ui::Row& row : m_presenter.ExecutionOrder())
    {
        const rename_win::CollectedItem* collected = nullptr;
        for (const rename_win::CollectedItem& entry : m_collected)
        {
            if (entry.id == row.id)
            {
                collected = &entry;
                break;
            }
        }
        if (!collected)
            continue;

        rename_win::ExecuteItem item;
        item.fullPath = collected->fullPath;
        item.targetLeaf = row.targetLeaf;
        item.isFolder = row.isFolder;
        item.depth = row.depth;
        batch.push_back(item);

        applied.emplace_back(row.id, row.targetLeaf);
    }

    if (batch.empty())
        return false;

    const rename_win::ExecuteResult result = rename_win::Execute(batch, m_dataDir);

    int renamed = 0;
    for (const rename_win::ExecuteResultItem& item : result.items)
    {
        if (item.outcome == rename_win::ExecuteOutcome::Renamed)
            ++renamed;
    }

    // Only rows that really renamed get their new name folded into the model;
    // folding a failed one would make a second attempt target the wrong name.
    //
    // Match each successful result back to the row by comparing the planned target
    // leaf, because Execute reports paths rather than row ids.
    std::vector<std::pair<std::string, std::wstring>> successful;
    for (const rename_win::ExecuteResultItem& item : result.items)
    {
        if (item.outcome != rename_win::ExecuteOutcome::Renamed)
            continue;

        for (const auto& [id, leaf] : applied)
        {
            if (item.targetPath.size() >= leaf.size() &&
                item.targetPath.compare(item.targetPath.size() - leaf.size(), leaf.size(), leaf) == 0)
            {
                successful.emplace_back(id, leaf);
                break;
            }
        }
    }

    m_presenter.ApplyExecutedResults(successful);

    // The batch is over; the table is empty now, so say what happened instead of
    // letting the empty state claim no files were ever selected.
    const int problems = static_cast<int>(result.items.size()) - renamed;
    m_doneLine = m_locale.startsWith(QLatin1String("zh"))
        ? QStringLiteral("已重命名 %1 项%2").arg(renamed)
              .arg(problems > 0 ? QStringLiteral("，%1 项未成功").arg(problems) : QString())
        : QStringLiteral("Renamed %1 item(s)%2").arg(renamed)
              .arg(problems > 0 ? QStringLiteral(", %1 not done").arg(problems) : QString());

    PublishRows();
    emit executed(renamed, ToQt(result.batchJournalPath));
    emit changed();
    emit countsChanged();
    return true;
}

void Bridge::reset()
{
    m_collected.clear();
    m_doneLine.clear();
    m_presenter.SetItems({});
    PublishRows();
    emit changed();
    emit countsChanged();
}

void Bridge::Refresh()
{
    PublishRows();
    emit changed();
    emit countsChanged();
}

void Bridge::PublishRows()
{
    m_previewModel.setRows(m_presenter.VisibleRows());
}

} // namespace rename_qml
