// bridge.cpp — Qt adapter over rename_ui::Presenter (M4).
//
// Nothing here decides anything: it converts to and from Qt types, pushes edits
// into the presenter, and republishes the result. If a behaviour question ever
// arises while reading this file, the answer belongs in core/ or presenter.cpp.
#include "ui/bridge.h"
#include "ui/presenter.h"
#include "win/platform.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QStyle>

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

QColor LookColor(const QVariantMap& look, const char* key, const QColor& fallback)
{
    const auto it = look.constFind(QString::fromLatin1(key));
    if (it == look.cend())
        return fallback;
    const QString text = it->toString().trimmed();
    if (text.startsWith(QLatin1Char('#')) && text.size() == 9)
    {
        bool ok = false;
        const int a = text.mid(1, 2).toInt(&ok, 16);
        const int r = ok ? text.mid(3, 2).toInt(&ok, 16) : 0;
        const int g = ok ? text.mid(5, 2).toInt(&ok, 16) : 0;
        const int b = ok ? text.mid(7, 2).toInt(&ok, 16) : 0;
        if (ok)
            return QColor(r, g, b, a);
    }
    const QColor parsed(text);
    return parsed.isValid() ? parsed : fallback;
}

} // namespace

// ---- PreviewModel ------------------------------------------------------------

PreviewModel::PreviewModel(QObject* parent) : QAbstractListModel(parent) {}

int PreviewModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int PreviewModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 4;
}

void PreviewModel::setColors(const QColor& text, const QColor& subtext,
                             const QColor& faint, const QColor& danger)
{
    m_text = text;
    m_subtext = subtext;
    m_faint = faint;
    m_danger = danger;
    if (!m_rows.empty())
        emit dataChanged(index(0, 0), index(static_cast<int>(m_rows.size()) - 1, 3));
}

QString PreviewModel::targetText(const rename_ui::Row& row) const
{
    const bool zh = m_locale.startsWith(QLatin1String("zh"));
    const QString target = ToQt(row.targetLeaf);
    switch (row.status)
    {
    case rename_core::Status::ErrorAlreadyExists:
        return zh ? QStringLiteral("目标已存在：") + target
                  : QStringLiteral("target exists: ") + target;
    case rename_core::Status::ErrorTooLong:
        return zh ? QStringLiteral("名字过长") : QStringLiteral("too long");
    case rename_core::Status::ErrorInvalidCharacter:
        return zh ? QStringLiteral("含非法字符") : QStringLiteral("invalid char");
    case rename_core::Status::ErrorEmptyName:
        return zh ? QStringLiteral("空名字") : QStringLiteral("empty name");
    case rename_core::Status::ErrorBadRule:
        return zh ? QStringLiteral("规则错误") : QStringLiteral("bad rule");
    default:
        return target;
    }
}

QVariant PreviewModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const rename_ui::Row& row = m_rows[static_cast<std::size_t>(index.row())];
    const bool error = row.status == rename_core::Status::ErrorAlreadyExists
                    || row.status == rename_core::Status::ErrorTooLong
                    || row.status == rename_core::Status::ErrorInvalidCharacter
                    || row.status == rename_core::Status::ErrorEmptyName
                    || row.status == rename_core::Status::ErrorBadRule;

    switch (role)
    {
    case IdRole:       return QString::fromStdString(row.id);
    case OriginalRole: return ToQt(row.originalLeaf);
    case TargetRole:   return ToQt(row.targetLeaf);
    case ParentRole:   return ToQt(row.parent);
    case StatusRole:   return StatusText(row.status);
    case SelectedRole: return row.selected;
    case IsFolderRole: return row.isFolder;
    case DepthRole:    return static_cast<int>(row.depth);
    case NoteRole:     return ToQt(row.note);
    case PendingDeleteRole: return row.pendingDelete;

    case Qt::CheckStateRole:
        return index.column() == 0 ? (row.selected ? Qt::Checked : Qt::Unchecked) : QVariant();
    case Qt::DisplayRole:
        if (index.column() == 0)
            return QString();
        if (index.column() == 1)
            return ToQt(row.originalLeaf);
        if (index.column() == 2 && row.pendingDelete)
            return m_locale.startsWith(QLatin1String("zh"))
                ? QStringLiteral("待删除（移至回收站）") : QStringLiteral("pending delete (Recycle Bin)");
        if (index.column() == 2)
            return targetText(row);
        if (index.column() == 3)
        {
            // An unchecked row is outside the batch; saying so is clearer than
            // showing a status the user cannot act on without checking it first.
            if (!row.selected)
                return m_locale.startsWith(QLatin1String("zh"))
                    ? QStringLiteral("未选") : QStringLiteral("Not selected");
            if (row.pendingDelete)
                return m_locale.startsWith(QLatin1String("zh"))
                    ? QStringLiteral("待删除") : QStringLiteral("Pending delete");
            if (error)
                return m_locale.startsWith(QLatin1String("zh"))
                    ? QStringLiteral("有问题") : QStringLiteral("Blocked");
            if (row.status == rename_core::Status::Ready)
                return m_locale.startsWith(QLatin1String("zh"))
                    ? QStringLiteral("可改") : QStringLiteral("Ready");
            return m_locale.startsWith(QLatin1String("zh"))
                ? QStringLiteral("不变") : QStringLiteral("Unchanged");
        }
        return QVariant();
    case Qt::DecorationRole:
        if (index.column() == 1 && QApplication::style())
            return QApplication::style()->standardIcon(row.isFolder ? QStyle::SP_DirIcon
                                                                    : QStyle::SP_FileIcon);
        return QVariant();
    case Qt::ForegroundRole:
        if (index.column() == 2)
        {
            if (error)
                return m_danger;
            if (row.pendingDelete)
                return m_danger;
            return row.status == rename_core::Status::Unchanged ? m_faint : m_text;
        }
        if (index.column() == 1)
            return error ? m_faint : m_text;
        return QVariant();
    case Qt::BackgroundRole:
        if (row.pendingDelete)
            return QColor(m_danger.red(), m_danger.green(), m_danger.blue(), 28);
        return QVariant();
    case Qt::FontRole:
        if (row.pendingDelete)
        {
            QFont font = m_baseFont;
            if (font.family().isEmpty())
                font = QApplication::font();
            font.setStrikeOut(true);
            return font;
        }
        if (index.column() == 2 && row.status == rename_core::Status::Ready)
        {
            QFont font = m_baseFont;
            if (font.family().isEmpty())
                font = QApplication::font();
            font.setBold(true);
            return font;
        }
        return QVariant();
    case Qt::TextAlignmentRole:
        return static_cast<int>(Qt::AlignVCenter | Qt::AlignLeft);
    case Qt::ToolTipRole:
        if (error)
            return targetText(row);
        // A folder on the row is elided to fit; the whole one is in the tooltip.
        if (index.column() == 1 && !row.parent.empty())
            return ToQt(row.parent);
        return row.note.empty() ? QVariant() : ToQt(row.note);
    default:
        return {};
    }
}

bool PreviewModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return false;
    if (index.column() != 0 || (role != SelectedRole && role != Qt::CheckStateRole))
        return false;

    rename_ui::Row& row = m_rows[static_cast<std::size_t>(index.row())];
    const bool selected = role == Qt::CheckStateRole ? value.toInt() == Qt::Checked : value.toBool();
    if (row.selected == selected)
        return false;

    row.selected = selected;
    emit dataChanged(index, index, {SelectedRole, Qt::CheckStateRole});
    emit selectionChanged(QString::fromStdString(row.id), selected);
    return true;
}

Qt::ItemFlags PreviewModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    // Only the first column is checkable; the names are read-only because editing
    // them is what the text mode is for (UI-SPEC.md §2.3).
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0)
        flags |= Qt::ItemIsUserCheckable;
    return flags;
}

QHash<int, QByteArray> PreviewModel::roleNames() const
{
    return {
        {IdRole, "rowId"},
        {OriginalRole, "originalName"},
        {TargetRole, "targetName"},
        {ParentRole, "parentFolder"},
        {StatusRole, "status"},
        {SelectedRole, "selected"},
        {IsFolderRole, "isFolder"},
        {DepthRole, "depth"},
        {NoteRole, "note"},
        {PendingDeleteRole, "pendingDelete"},
    };
}

void PreviewModel::setRows(const std::vector<rename_ui::Row>& rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

// ---- RuleListModel -----------------------------------------------------------

RuleListModel::RuleListModel(QObject* parent) : QAbstractListModel(parent) {}

int RuleListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rules.size());
}

QString RuleListModel::summary(const rename_core::Rule& rule) const
{
    const bool zh = m_locale.startsWith(QLatin1String("zh"));
    QString where;
    switch (rule.scope)
    {
    case rename_core::Scope::Name:      where = zh ? QStringLiteral("名称") : QStringLiteral("name"); break;
    case rename_core::Scope::Extension: where = zh ? QStringLiteral("扩展名") : QStringLiteral("ext"); break;
    case rename_core::Scope::Full:      where = zh ? QStringLiteral("全名") : QStringLiteral("full"); break;
    }
    const QString kind = rule.match == rename_core::MatchType::Regex
        ? (zh ? QStringLiteral("正则") : QStringLiteral("regex"))
        : (zh ? QStringLiteral("文本") : QStringLiteral("text"));
    QString body = ToQt(rule.search);
    if (!rule.replace.empty())
        body += QStringLiteral(" → ") + ToQt(rule.replace);
    if (body.isEmpty())
        body = zh ? QStringLiteral("(空的)") : QStringLiteral("(empty)");
    if (body.length() > 40)
        body = body.left(39) + QStringLiteral("…");
    return kind + QStringLiteral(" · ") + where + QStringLiteral(" · ") + body;
}

QVariant RuleListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rules.size()))
        return {};
    const rename_core::Rule& rule = m_rules[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case Qt::DisplayRole:
    case SummaryRole:
        return summary(rule);
    case Qt::CheckStateRole:
        return rule.enabled ? Qt::Checked : Qt::Unchecked;
    case EnabledRole:
        return rule.enabled;
    case ErrorRole:
        return ruleError(index.row());
    case Qt::ForegroundRole:
        // Bad pattern in red, disabled rules faint, everything else in the text
        // colour; the QListView paints the string it gets from DisplayRole.
        if (ruleError(index.row()))
            return QColor(QStringLiteral("#E5484D"));
        if (!rule.enabled)
            return QColor(128, 128, 128);
        return QVariant();
    case Qt::ToolTipRole:
        return summary(rule);
    default:
        return {};
    }
}

bool RuleListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rules.size()))
        return false;
    if (role != Qt::CheckStateRole && role != EnabledRole)
        return false;
    const bool enabled = role == Qt::CheckStateRole ? value.toInt() == Qt::Checked : value.toBool();
    if (m_rules[static_cast<std::size_t>(index.row())].enabled == enabled)
        return false;
    emit enabledToggled(index.row(), enabled);
    return true;
}

Qt::ItemFlags RuleListModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = QAbstractListModel::flags(index);
    if (index.isValid())
        flags |= Qt::ItemIsUserCheckable;
    return flags;
}

QHash<int, QByteArray> RuleListModel::roleNames() const
{
    return {
        {SummaryRole, "summary"},
        {EnabledRole, "enabled"},
        {ErrorRole, "error"},
    };
}

void RuleListModel::setRules(const std::vector<rename_core::Rule>& rules, const QString& locale)
{
    beginResetModel();
    m_rules = rules;
    m_locale = locale;
    endResetModel();
}

bool RuleListModel::ruleError(int index) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_rules.size())
        return false;
    const rename_core::Rule& rule = m_rules[static_cast<std::size_t>(index)];
    return rule.match == rename_core::MatchType::Regex && !rule.search.empty()
        && !rename_core::RegexCompiles(rule.search, (rule.flags & rename_core::FlagCaseSensitive) != 0);
}

bool RuleListModel::ruleEnabled(int index) const
{
    return index >= 0 && static_cast<std::size_t>(index) < m_rules.size()
        && m_rules[static_cast<std::size_t>(index)].enabled;
}

// ---- Bridge ------------------------------------------------------------------

Bridge::Bridge(QObject* parent) : QObject(parent)
{
    // A window opened on nothing still shows one empty rule: the detail editor
    // has something to bind to, and the first keystroke is an edit rather than
    // an "add rule" click. An empty search matches nothing in the core, so the
    // empty default is inert.
    rename_core::Rule initial;
    initial.enabled = true;
    initial.occurrence = rename_core::Occurrence::All;
    m_chain.rules.push_back(std::move(initial));
    m_selectedRule = 0;

    connect(&m_previewModel, &PreviewModel::selectionChanged, this,
            [this](const QString& id, bool selected) {
                m_presenter.SetRowSelected(id.toStdString(), selected);
                if (m_presenter.CurrentMode() == rename_ui::Mode::Text)
                    emit changed(); // SyncTextEditor() reprojects / versus //
                PublishRows();
                emit countsChanged();
                emit changed();
            });
    connect(&m_ruleModel, &RuleListModel::enabledToggled, this, [this](int index, bool enabled) {
        if (index < 0 || static_cast<std::size_t>(index) >= m_chain.rules.size())
            return;
        m_chain.rules[static_cast<std::size_t>(index)].enabled = enabled;
        SyncControlState();
        emit ruleChanged();
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
    // The rule summaries are localized strings; repaint them in the new language.
    m_ruleModel.setRules(m_chain.rules, m_locale);

    const bool isDark = m_dark;
    m_previewModel.setLocale(m_locale);
    m_previewModel.setColors(
        LookColor(m_look, "text", isDark ? QColor(QStringLiteral("#F3F3F3")) : QColor(QStringLiteral("#1B1B1B"))),
        LookColor(m_look, "subtext", isDark ? QColor(QStringLiteral("#B8B8B8")) : QColor(QStringLiteral("#666666"))),
        LookColor(m_look, "faint", QColor(QStringLiteral("#8A8A8A"))),
        LookColor(m_look, "danger", isDark ? QColor(QStringLiteral("#FF99A4")) : QColor(QStringLiteral("#C42B1C"))));
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
    // A selection that never became a set is a real error, not "no files yet".
    if (!m_blockedLine.isEmpty())
        return m_blockedLine;

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
int Bridge::deleteCount() const { return m_presenter.CurrentCounts().toDelete; }
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

bool Bridge::ruleCaseSensitive() const
{
    return (m_flags & rename_core::FlagCaseSensitive) != 0;
}

void Bridge::setRuleCaseSensitive(bool caseSensitive)
{
    if (caseSensitive == ruleCaseSensitive())
        return;
    if (caseSensitive)
        m_flags |= rename_core::FlagCaseSensitive;
    else
        m_flags &= ~static_cast<std::uint32_t>(rename_core::FlagCaseSensitive);
    SyncControlState();
    emit ruleChanged();
    emit changed();
    emit countsChanged();
}

bool Bridge::ruleRegexInvalid() const
{
    // "Regex" is on and the pattern does not compile: the field turns red before
    // the user presses anything, which is the only useful time to say so. A bad
    // pattern surfaces as bad-rule rows, so the plan is what to ask.
    const int index = m_selectedRule;
    if (index < 0 || static_cast<std::size_t>(index) >= m_chain.rules.size())
        return false;
    const rename_core::Rule& rule = m_chain.rules[static_cast<std::size_t>(index)];
    if (rule.match != rename_core::MatchType::Regex || rule.search.empty())
        return false;
    return !rename_core::RegexCompiles(rule.search, (rule.flags & rename_core::FlagCaseSensitive) != 0);
}

QString Bridge::filter() const
{
    switch (m_presenter.CurrentFilter())
    {
    case rename_ui::Filter::Selected:    return QStringLiteral("selected");
    case rename_ui::Filter::Applicable:  return QStringLiteral("applicable");
    case rename_ui::Filter::ToRename:    return QStringLiteral("to-rename");
    case rename_ui::Filter::All:         break;
    }
    return QStringLiteral("all");
}

PreviewModel* Bridge::previewModel() { return &m_previewModel; }

QString Bridge::pathForRow(int visibleRow) const
{
    const std::vector<rename_ui::Row> rows = m_presenter.VisibleRows();
    if (visibleRow < 0 || static_cast<std::size_t>(visibleRow) >= rows.size())
        return {};
    const std::string& id = rows[static_cast<std::size_t>(visibleRow)].id;
    for (const rename_win::CollectedItem& item : m_collected)
    {
        if (item.id == id)
            return ToQt(item.fullPath);
    }
    return {};
}

bool Bridge::rowIsFolder(int visibleRow) const
{
    const std::vector<rename_ui::Row> rows = m_presenter.VisibleRows();
    return visibleRow >= 0 && static_cast<std::size_t>(visibleRow) < rows.size()
        && rows[static_cast<std::size_t>(visibleRow)].isFolder;
}

bool Bridge::hasUncommittedEdits() const
{
    return m_presenter.HasUncommittedEdits();
}

void Bridge::openFolder(const QString& folder)
{
    if (!folder.isEmpty() && QFileInfo(folder).isDir())
        setFolder(folder, false);
}

void Bridge::goUp()
{
    if (m_folder.isEmpty())
        return;
    const QFileInfo current(m_folder);
    const QString parent = current.absolutePath();
    if (!parent.isEmpty() && parent.compare(m_folder, Qt::CaseInsensitive) != 0)
        setFolder(parent, false);
}

void Bridge::reloadFolder()
{
    if (!m_folder.isEmpty())
        setFolder(m_folder, false);
}

void Bridge::setSelection(const QStringList& paths, bool recursive)
{
    // A new host selection is a new task: clear the previous report, filter and
    // text buffer first. Without this a second right-click showed "Renamed N"
    // and the old oil buffer beside the new files.
    m_presenter.SetMode(rename_ui::Mode::Control);
    m_presenter.SetBuffer({});
    m_presenter.SetOilTable({});
    m_presenter.SetFilter(rename_ui::Filter::All);
    m_doneLine.clear();
    m_blockedLine.clear();
    m_folder.clear();
    m_collected.clear();

    // The path bar names one folder; a batch that spans folders (a keyword
    // search, or a multi-select) has none, and the rows carry their own folder
    // instead. A single folder path still shows the folder it sits in, which is
    // what "up" and "reload" work from.
    if (!paths.isEmpty())
    {
        m_folder = QFileInfo(paths.first()).absolutePath();
        for (const QString& path : paths)
        {
            if (QFileInfo(path).absolutePath().compare(m_folder, Qt::CaseInsensitive) != 0)
            {
                m_folder.clear();
                break;
            }
        }
    }

    // The wrapper opens an empty window to show the empty state. An empty
    // selection is not an enumeration error.
    if (paths.isEmpty())
    {
        m_presenter.SetItems({});
        PublishRows();
        emit changed();
        emit countsChanged();
        return;
    }

    rename_win::EnumerateRequest request;
    request.recursive = recursive;
    for (const QString& path : paths)
        request.roots.push_back(FromQt(path));

    const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
    if (collected.blocked)
    {
        m_doneLine.clear();
        m_blockedLine = ToQt(collected.blockReason);
        m_presenter.SetItems({});
        PublishRows();
        emit changed();
        emit countsChanged();
        return;
    }
    m_collected = collected.items;

    std::vector<rename_core::ItemSnapshot> items;
    items.reserve(collected.items.size());
    for (const rename_win::CollectedItem& entry : collected.items)
    {
        rename_core::ItemSnapshot snapshot;
        snapshot.id = entry.id;
        snapshot.leaf = entry.leaf;
        snapshot.parent = entry.parent;
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
    //
    // Navigation keeps the tab the user is on: going up from the text mode has
    // to leave them in the text mode, with the new folder's items in the buffer.
    // The old buffer described the old set, so it is rebuilt either way.
    const bool text = m_presenter.CurrentMode() == rename_ui::Mode::Text;
    m_presenter.SetBuffer({});
    m_presenter.SetOilTable({});
    m_presenter.SetFilter(rename_ui::Filter::All);
    m_doneLine.clear();
    m_blockedLine.clear();
    m_collected.clear();
    m_folder = folder;

    rename_win::EnumerateRequest request;
    request.recursive = recursive;
    request.listContents = true;
    request.roots.push_back(FromQt(folder));

    const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
    if (collected.blocked)
    {
        m_doneLine.clear();
        m_blockedLine = ToQt(collected.blockReason);
        m_presenter.SetItems({});
        if (text)
            m_presenter.SetMode(rename_ui::Mode::Text);
        PublishRows();
        emit changed();
        emit countsChanged();
        return;
    }
    m_collected = collected.items;

    std::vector<rename_core::ItemSnapshot> items;
    items.reserve(collected.items.size());
    for (const rename_win::CollectedItem& entry : collected.items)
    {
        rename_core::ItemSnapshot snapshot;
        snapshot.id = entry.id;
        snapshot.leaf = entry.leaf;
        snapshot.parent = entry.parent;
        snapshot.isFolder = entry.isFolder;
        snapshot.depth = entry.depth;
        snapshot.pathLength = entry.pathLength;
        snapshot.templateContext = rename_win::BuildTemplateContext(entry, 0);
        items.push_back(std::move(snapshot));
    }

    m_presenter.ReplaceItems(std::move(items));
    if (text)
    {
        m_presenter.SetMode(rename_ui::Mode::Text);
        BuildOilTable();
    }
    m_folder = folder;
    PublishRows();
    emit changed();
    emit countsChanged();
}

bool Bridge::switchMode(const QString& target)
{
    return SwitchMode(target, false);
}

bool Bridge::forceSwitchMode(const QString& target)
{
    return SwitchMode(target, true);
}

bool Bridge::SwitchMode(const QString& target, bool force)
{
    const rename_ui::Mode wanted = (target == QStringLiteral("text")) ? rename_ui::Mode::Text
                                                                     : rename_ui::Mode::Control;
    if (!force && !m_presenter.CanSwitchMode(wanted))
        return false;
    m_presenter.SetMode(wanted);
    if (wanted == rename_ui::Mode::Text && m_presenter.Buffer().empty())
        BuildOilTable();
    PublishRows();
    emit changed();
    return true;
}

void Bridge::BuildOilTable()
{
    // First entry into the text mode: build the buffer from the input set, one
    // "/<id> <name>" line per item, so the user starts from what the host handed
    // over rather than from an empty editor.
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

void Bridge::markRowForDelete(const QString& id)
{
    if (m_presenter.CurrentMode() != rename_ui::Mode::Text)
        return;

    // "dd" rewrites the row's state letter to "d" instead of removing the line:
    // the delete stays visible in the text, so it can be undone with "u", with the
    // table's context menu, or by typing "y" back over it.
    const QStringList lines = buffer().split(QLatin1Char('\n'));
    const QString marker = QLatin1Char(' ') + id + QLatin1Char(' ');
    QStringList updated;
    bool marked = false;
    for (const QString& line : lines)
    {
        if (!marked && line.size() > 2 && line.at(1).isSpace() && line.mid(1).startsWith(marker))
        {
            updated.push_back(QLatin1Char('d') + line.mid(1));
            marked = true;
            continue;
        }
        updated.push_back(line);
    }
    if (!marked)
        return;
    m_presenter.SetBuffer(FromQt(updated.join(QLatin1Char('\n'))));
    PublishRows();
    emit changed();
    emit countsChanged();
}

bool Bridge::revertRow(const QString& id)
{
    const bool reverted = m_presenter.RevertRow(id.toStdString());
    if (!reverted)
        return false;
    PublishRows();
    emit changed();
    emit countsChanged();
    return true;
}

bool Bridge::revertAllEdits()
{
    const bool reverted = m_presenter.RevertAllEdits();
    if (!reverted)
        return false;
    PublishRows();
    emit changed();
    emit countsChanged();
    return true;
}

bool Bridge::restorePendingDelete(const QString& id)
{
    const bool restored = m_presenter.RestorePendingDelete(id.toStdString());
    if (!restored)
        return false;
    PublishRows();
    emit changed();
    emit countsChanged();
    return true;
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

namespace {

rename_core::Rule RuleFrom(int scope, int match, const std::wstring& find, const std::wstring& replace,
                           bool enabled, bool matchAll, std::uint32_t batchFlags)
{
    rename_core::Rule rule;
    rule.enabled = enabled;
    rule.scope = (scope == 1) ? rename_core::Scope::Extension
                              : (scope == 2 ? rename_core::Scope::Full : rename_core::Scope::Name);
    rule.match = (match == 1) ? rename_core::MatchType::Regex : rename_core::MatchType::Literal;
    rule.occurrence = matchAll ? rename_core::Occurrence::All : rename_core::Occurrence::First;
    rule.search = find;
    rule.replace = replace;
    // Case sensitivity is a flag on the whole batch, as PowerRename's checkbox is
    // (not per rule): one batch compares names one way.
    rule.flags = batchFlags & rename_core::FlagCaseSensitive;
    return rule;
}

} // namespace

void Bridge::setCaseTransform(int which)
{
    // A case transform is a batch-level flag in the core, not a rule: it applies
    // to whatever the rules produced. `which` is the chip pressed, and pressing
    // the same chip again turns it off, which is what a toggle should do.
    const std::uint32_t all = rename_core::FlagUppercase | rename_core::FlagLowercase
                            | rename_core::FlagTitleCase | rename_core::FlagCapitalize;

    const std::uint32_t previous = m_flags & all;
    m_flags &= ~all;

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
        m_flags |= wanted;

    m_caseTransform = (wanted != 0 && previous != wanted) ? which : 0;

    SyncControlState();
    emit ruleChanged();
    emit changed();
    emit countsChanged();
}

void Bridge::SyncControlState()
{
    rename_ui::ControlState state;
    state.chain = m_chain;
    state.flags = m_flags;
    m_presenter.SetControlState(std::move(state));
    m_ruleModel.setRules(m_chain.rules, m_locale);
    PublishRows();
    emit changed();
    emit countsChanged();
}

QVariantMap Bridge::ruleAt(int index) const
{
    QVariantMap map;
    if (index < 0 || static_cast<std::size_t>(index) >= m_chain.rules.size())
        return map;
    const rename_core::Rule& rule = m_chain.rules[static_cast<std::size_t>(index)];
    map.insert(QStringLiteral("scope"), rule.scope == rename_core::Scope::Extension ? 1
                                   : rule.scope == rename_core::Scope::Full ? 2 : 0);
    map.insert(QStringLiteral("match"), rule.match == rename_core::MatchType::Regex ? 1 : 0);
    map.insert(QStringLiteral("matchAll"), rule.occurrence == rename_core::Occurrence::All);
    map.insert(QStringLiteral("find"), ToQt(rule.search));
    map.insert(QStringLiteral("replace"), ToQt(rule.replace));
    map.insert(QStringLiteral("enabled"), rule.enabled);
    map.insert(QStringLiteral("error"), m_ruleModel.ruleError(index));
    return map;
}

void Bridge::setRuleAt(int index, int scope, int match, const QString& find,
                       const QString& replace, bool enabled, bool matchAll)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_chain.rules.size())
        return;
    m_chain.rules[static_cast<std::size_t>(index)] =
        RuleFrom(scope, match, FromQt(find), FromQt(replace), enabled, matchAll, m_flags);
    SyncControlState();
    emit ruleChanged();
}

void Bridge::selectRule(int index)
{
    const int clamped = (index >= 0 && index < ruleCount()) ? index : -1;
    if (clamped == m_selectedRule)
        return;
    m_selectedRule = clamped;
    emit ruleChanged();
}

int Bridge::addRule()
{
    rename_core::Rule rule;
    rule.enabled = true;
    rule.occurrence = rename_core::Occurrence::All;
    rule.flags = m_flags & rename_core::FlagCaseSensitive;
    m_chain.rules.push_back(std::move(rule));
    m_selectedRule = ruleCount() - 1;
    SyncControlState();
    emit ruleChanged();
    return m_selectedRule;
}

void Bridge::duplicateRule(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_chain.rules.size())
        return;
    m_chain.rules.insert(m_chain.rules.begin() + index + 1,
                         m_chain.rules[static_cast<std::size_t>(index)]);
    m_selectedRule = index + 1;
    SyncControlState();
    emit ruleChanged();
}

void Bridge::removeRule(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_chain.rules.size())
        return;
    m_chain.rules.erase(m_chain.rules.begin() + index);
    if (m_selectedRule >= ruleCount())
        m_selectedRule = ruleCount() - 1;
    SyncControlState();
    emit ruleChanged();
}

void Bridge::moveRule(int from, int to)
{
    const int count = ruleCount();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return;
    rename_core::Rule rule = m_chain.rules[static_cast<std::size_t>(from)];
    m_chain.rules.erase(m_chain.rules.begin() + from);
    m_chain.rules.insert(m_chain.rules.begin() + to, std::move(rule));
    if (m_selectedRule == from)
        m_selectedRule = to;
    SyncControlState();
    emit ruleChanged();
}

// ---- presets -----------------------------------------------------------------
//
// A preset is a named chain plus the batch flags it was saved with. The file is
// <dataDir>/presets.json; the first read seeds three built-ins so the menu is
// never empty.

namespace {

const wchar_t* const kPresetSpaceUnderscore[] = {L" ", L"_", nullptr};
const wchar_t* const kPresetTrimSpaces[] = {L"\\s+", L" ", nullptr};
const wchar_t* const kPresetNumber[] = {L"^", L"${start=1,increment=1,padding=3}-", nullptr};

} // namespace

void Bridge::LoadPresets() const
{
    m_presetsLoaded = true;
    m_presets.clear();

    const QString path = QDir::cleanPath(ToQt(m_dataDir) + QStringLiteral("/presets.json"));
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
    {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonArray array = doc.object().value(QStringLiteral("presets")).toArray();
        for (const QJsonValue& value : array)
        {
            const QJsonObject obj = value.toObject();
            Preset preset;
            preset.name = obj.value(QStringLiteral("name")).toString();
            preset.flags = static_cast<std::uint32_t>(
                obj.value(QStringLiteral("flags")).toInt());
            const QJsonArray rules = obj.value(QStringLiteral("rules")).toArray();
            for (const QJsonValue& ruleValue : rules)
            {
                const QJsonObject ruleObj = ruleValue.toObject();
                rename_core::Rule rule;
                rule.enabled = ruleObj.value(QStringLiteral("enabled")).toBool(true);
                const QString scope = ruleObj.value(QStringLiteral("scope")).toString();
                rule.scope = scope == QStringLiteral("extension") ? rename_core::Scope::Extension
                           : scope == QStringLiteral("full") ? rename_core::Scope::Full
                           : rename_core::Scope::Name;
                rule.match = ruleObj.value(QStringLiteral("match")).toString()
                             == QStringLiteral("regex")
                             ? rename_core::MatchType::Regex : rename_core::MatchType::Literal;
                rule.occurrence = ruleObj.value(QStringLiteral("occurrence")).toString()
                                  == QStringLiteral("first")
                                  ? rename_core::Occurrence::First : rename_core::Occurrence::All;
                rule.search = FromQt(ruleObj.value(QStringLiteral("search")).toString());
                rule.replace = FromQt(ruleObj.value(QStringLiteral("replace")).toString());
                preset.chain.rules.push_back(std::move(rule));
            }
            if (!preset.name.isEmpty() && !preset.chain.rules.empty())
                m_presets.push_back(std::move(preset));
        }
        if (!m_presets.empty())
            return;
    }

    // Seed the built-ins: three one-rule chains that cover the most common asks.
    auto seed = [this](const QString& name, const wchar_t* const* parts, int scope, int match) {
        Preset preset;
        preset.name = name;
        preset.chain.rules.push_back(
            RuleFrom(scope, match, parts[0], parts[1], true, /*matchAll=*/true, 0));
        m_presets.push_back(std::move(preset));
    };
    seed(QStringLiteral("空格 → 下划线"), kPresetSpaceUnderscore, 0, 0);
    seed(QStringLiteral("合并连续空格"), kPresetTrimSpaces, 0, 1);
    seed(QStringLiteral("编号 001-"), kPresetNumber, 2, 1);
    StorePresets();
}

void Bridge::StorePresets() const
{
    if (m_dataDir.empty())
        return;
    QJsonArray array;
    for (const Preset& preset : m_presets)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), preset.name);
        obj.insert(QStringLiteral("flags"), static_cast<int>(preset.flags));
        QJsonArray rules;
        for (const rename_core::Rule& rule : preset.chain.rules)
        {
            QJsonObject ruleObj;
            ruleObj.insert(QStringLiteral("enabled"), rule.enabled);
            ruleObj.insert(QStringLiteral("scope"),
                           rule.scope == rename_core::Scope::Extension ? QStringLiteral("extension")
                           : rule.scope == rename_core::Scope::Full ? QStringLiteral("full")
                           : QStringLiteral("name"));
            ruleObj.insert(QStringLiteral("match"),
                           rule.match == rename_core::MatchType::Regex ? QStringLiteral("regex")
                           : QStringLiteral("literal"));
            ruleObj.insert(QStringLiteral("occurrence"),
                           rule.occurrence == rename_core::Occurrence::First
                           ? QStringLiteral("first") : QStringLiteral("all"));
            ruleObj.insert(QStringLiteral("search"), ToQt(rule.search));
            ruleObj.insert(QStringLiteral("replace"), ToQt(rule.replace));
            rules.append(ruleObj);
        }
        obj.insert(QStringLiteral("rules"), rules);
        array.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("presets"), array);
    QDir().mkpath(ToQt(m_dataDir));
    QFile file(QDir::cleanPath(ToQt(m_dataDir) + QStringLiteral("/presets.json")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QStringList Bridge::presetNames() const
{
    if (!m_presetsLoaded)
        LoadPresets();
    QStringList names;
    for (const Preset& preset : m_presets)
        names << preset.name;
    return names;
}

void Bridge::loadPreset(const QString& name)
{
    if (!m_presetsLoaded)
        LoadPresets();
    for (const Preset& preset : m_presets)
    {
        if (preset.name != name)
            continue;
        m_chain = preset.chain;
        m_flags = preset.flags;
        m_caseTransform = (m_flags & rename_core::FlagLowercase) ? 1
                        : (m_flags & rename_core::FlagUppercase) ? 2
                        : (m_flags & rename_core::FlagCapitalize) ? 3
                        : (m_flags & rename_core::FlagTitleCase) ? 4 : 0;
        m_selectedRule = ruleCount() > 0 ? 0 : -1;
        SyncControlState();
        emit ruleChanged();
        return;
    }
}

bool Bridge::savePreset(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || m_chain.rules.empty() || m_dataDir.empty())
        return false;
    if (!m_presetsLoaded)
        LoadPresets();
    for (Preset& preset : m_presets)
    {
        if (preset.name == trimmed)
        {
            preset.chain = m_chain;
            preset.flags = m_flags;
            StorePresets();
            return true;
        }
    }
    Preset preset;
    preset.name = trimmed;
    preset.chain = m_chain;
    preset.flags = m_flags;
    m_presets.push_back(std::move(preset));
    StorePresets();
    return true;
}

void Bridge::deletePreset(const QString& name)
{
    if (!m_presetsLoaded)
        LoadPresets();
    for (auto it = m_presets.begin(); it != m_presets.end(); ++it)
    {
        if (it->name == name)
        {
            m_presets.erase(it);
            StorePresets();
            return;
        }
    }
}

// ---- memory of per-user preferences ---------------------------------------------
//
// One small file, <dataDir>/settings.json. It is read once (lazily, so a bridge
// with no data directory — tests, the grab hook — never touches the disk) and
// written on every change. A missing or malformed file means "defaults", never
// an error: a preference is not worth blocking a rename over.

namespace {

QString SettingsPath(const std::wstring& dataDir)
{
    return QDir::cleanPath(ToQt(dataDir) + QStringLiteral("/settings.json"));
}

} // namespace

bool Bridge::vimEnabled() const
{
    if (!m_settingsLoaded)
    {
        m_settingsLoaded = true;
        if (!m_dataDir.empty())
        {
            QFile file(SettingsPath(m_dataDir));
            if (file.open(QIODevice::ReadOnly))
            {
                const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
                m_vimEnabled = root.value(QStringLiteral("vimMode")).toBool(false);
            }
        }
    }
    return m_vimEnabled;
}

void Bridge::setVimEnabled(bool enabled)
{
    if (vimEnabled() == enabled)
        return;
    m_vimEnabled = enabled;
    if (!m_dataDir.empty())
    {
        const QString path = SettingsPath(m_dataDir);
        QJsonObject root;
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly))
            root = QJsonDocument::fromJson(existing.readAll()).object();
        existing.close();
        root.insert(QStringLiteral("vimMode"), enabled);
        QDir().mkpath(ToQt(m_dataDir));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    emit changed();
}

void Bridge::setSingleRule(int scope, int match, const QString& find,
                           const QString& replace, bool enabled)
{
    // The grab hook's one-rule form: the whole chain becomes this single rule.
    m_chain.rules.clear();
    m_chain.rules.push_back(
        RuleFrom(scope, match, FromQt(find), FromQt(replace), enabled,
                 /*matchAll=*/true, m_flags));
    m_selectedRule = ruleCount() - 1;
    SyncControlState();
    emit ruleChanged();
}

bool Bridge::apply()
{
    if (!m_presenter.CanExecute())
        return false;

    std::vector<rename_win::ExecuteItem> batch;
    struct Applied
    {
        std::string id;
        std::wstring leaf;
        QString originalPath;
        QString expectedPath;
        bool deleteItem = false;
    };
    std::vector<Applied> applied;

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
        item.deleteItem = row.pendingDelete;
        item.isFolder = row.isFolder;
        item.depth = row.depth;
        batch.push_back(item);

        const QString original = QString::fromStdWString(collected->fullPath);
        const QString parent = QFileInfo(original).absolutePath();
        applied.push_back({row.id, row.targetLeaf, QDir::cleanPath(original),
                           row.pendingDelete ? QString()
                                             : QDir::cleanPath(parent + QLatin1Char('/')
                                                                + QString::fromStdWString(row.targetLeaf)),
                           row.pendingDelete});
    }

    if (batch.empty())
        return false;

    const rename_win::ExecuteResult result = rename_win::Execute(batch, m_dataDir);

    int renamed = 0;
    int deleted = 0;

    // Only rows that really renamed get their new name folded into the model;
    // folding a failed one would make a second attempt target the wrong name.
    //
    // Match each successful result back to the row by the full planned target
    // path. The old suffix comparison could confuse two folders with the same
    // leaf name.
    std::vector<std::pair<std::string, std::wstring>> successful;
    std::vector<std::string> deletedIds;
    QStringList failureDetails;
    for (const rename_win::ExecuteResultItem& item : result.items)
    {
        if (item.outcome != rename_win::ExecuteOutcome::Renamed)
        {
            if (!item.detail.empty())
                failureDetails.push_back(ToQt(item.detail));
            continue;
        }

        const QString actual = QDir::cleanPath(QString::fromStdWString(item.targetPath));
        for (const Applied& appliedItem : applied)
        {
            if (appliedItem.deleteItem
                && QString::compare(QDir::cleanPath(QString::fromStdWString(item.originalPath)),
                                    appliedItem.originalPath, Qt::CaseInsensitive) == 0)
            {
                deletedIds.push_back(appliedItem.id);
                ++deleted;
                break;
            }
            if (!appliedItem.deleteItem
                && QString::compare(actual, appliedItem.expectedPath, Qt::CaseInsensitive) == 0)
            {
                successful.emplace_back(appliedItem.id, appliedItem.leaf);
                ++renamed;
                break;
            }
        }
    }

    m_presenter.ApplyExecutedResults(successful, deletedIds);
    // Keep the execution source map in sync with the visible completed list so a
    // second batch resolves the renamed file at its current path.
    for (const auto& [id, leaf] : successful)
    {
        for (rename_win::CollectedItem& entry : m_collected)
        {
            if (entry.id == id)
            {
                entry.leaf = leaf;
                const QString parent = QFileInfo(QString::fromStdWString(entry.fullPath)).absolutePath();
                entry.fullPath = FromQt(QDir::cleanPath(parent + QLatin1Char('/')
                                                        + QString::fromStdWString(leaf)));
                break;
            }
        }
    }
    m_collected.erase(std::remove_if(m_collected.begin(), m_collected.end(),
                                    [&](const rename_win::CollectedItem& entry) {
                                        return std::find(deletedIds.begin(), deletedIds.end(), entry.id)
                                            != deletedIds.end();
                                    }),
                      m_collected.end());

    // The batch is over; the table is empty now, so say what happened instead of
    // letting the empty state claim no files were ever selected.
    const int problems = static_cast<int>(result.items.size()) - renamed - deleted;
    m_blockedLine.clear();
    m_doneLine = m_locale.startsWith(QLatin1String("zh"))
        ? QStringLiteral("已重命名 %1 项，已删除 %2 项%3").arg(renamed).arg(deleted)
              .arg(problems > 0 ? QStringLiteral("，%1 项未成功").arg(problems) : QString())
        : QStringLiteral("Renamed %1 item(s), deleted %2 item(s)%3").arg(renamed).arg(deleted)
              .arg(problems > 0 ? QStringLiteral(", %1 not done").arg(problems) : QString());
    if (!failureDetails.empty())
        m_doneLine += QLatin1Char('\n') + failureDetails.join(QStringLiteral("；"));

    PublishRows();
    emit executed(renamed + deleted, ToQt(result.batchJournalPath));
    emit changed();
    emit countsChanged();
    return true;
}

void Bridge::reset()
{
    m_collected.clear();
    m_doneLine.clear();
    m_blockedLine.clear();
    m_folder.clear();
    m_presenter.SetMode(rename_ui::Mode::Control);
    m_presenter.SetBuffer({});
    m_presenter.SetOilTable({});
    m_presenter.SetFilter(rename_ui::Filter::All);
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
