// bridge.h — the Qt-facing half of the UI (M4).
//
// This exists only when Qt is available (RENAME_BUILD_QT in CMakeLists.txt). It
// is a thin adapter: every decision lives in rename_ui::Presenter, and this class
// only exposes that state to QML and forwards edits back. Keeping it thin is what
// lets the entire UI behaviour be tested with no Qt installed.
//
// Two QML-facing models:
//   * ruleModel   — the rule chain rows
//   * previewModel — the preview table rows
//
// Both are list models over Presenter state, so QML never owns truth.
#pragma once

#include "ui/presenter.h"
#include "win/platform.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QAbstractListModel>

namespace rename_qml {

// Rows for the preview table. Exposes exactly the columns UI-SPEC.md asks for.
class PreviewModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        OriginalRole,
        TargetRole,
        StatusRole,
        SelectedRole,
        IsFolderRole,
        DepthRole,
        NoteRole,
        SkippedRole,
    };

    explicit PreviewModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const std::vector<rename_ui::Row>& rows);
    std::vector<rename_ui::Row> rows() const { return m_rows; }
    void setLocale(const QString& locale) { m_locale = locale; }
    void setBaseFont(const QFont& font) { m_baseFont = font; }
    void setColors(const QColor& text, const QColor& subtext, const QColor& faint, const QColor& danger);

signals:
    // Emitted when the user toggles a checkbox, so the bridge can push the
    // change back into the presenter and refresh the counts.
    void selectionChanged(const QString& id, bool selected);

private:
    QString targetText(const rename_ui::Row& row) const;

    std::vector<rename_ui::Row> m_rows;
    QString m_locale = QStringLiteral("zh");
    QFont m_baseFont;
    QColor m_text;
    QColor m_subtext;
    QColor m_faint;
    QColor m_danger;
};

// Rows for the rule chain list. One row per rule in the chain, in chain order;
// the summary line is what the row paints ("名称 · foo → bar"), the checkbox is
// the rule's enabled flag.
class RuleListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        SummaryRole = Qt::UserRole + 1,
        EnabledRole,
        ErrorRole,
    };

    explicit RuleListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRules(const std::vector<rename_core::Rule>& rules, const QString& locale);
    bool ruleError(int index) const;
    bool ruleEnabled(int index) const;

signals:
    // The checkbox on a row was toggled; the Bridge owns the chain and applies it.
    void enabledToggled(int index, bool enabled);

private:
    QString summary(const rename_core::Rule& rule) const;

    std::vector<rename_core::Rule> m_rules;
    QString m_locale = QStringLiteral("zh");
};

// The window's presenter front end.
class Bridge : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString buffer READ buffer WRITE setBuffer NOTIFY changed)
    Q_PROPERTY(QString blockingReason READ blockingReason NOTIFY changed)
    Q_PROPERTY(int readyCount READ readyCount NOTIFY countsChanged)
    Q_PROPERTY(int unchangedCount READ unchangedCount NOTIFY countsChanged)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY countsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY countsChanged)
    Q_PROPERTY(bool canExecute READ canExecute NOTIFY changed)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(bool allSelected READ allSelected NOTIFY countsChanged)
    Q_PROPERTY(bool ruleRegexInvalid READ ruleRegexInvalid NOTIFY ruleChanged)
    Q_PROPERTY(QString doneLine READ doneLine NOTIFY changed)
    Q_PROPERTY(PreviewModel* previewModel READ previewModel CONSTANT)

    // The host's look (docs/plugins.md, 外观): `look` is `appearance.nativeLook`
    // verbatim, so QML reads the style's own colours and radii and keeps a
    // built-in fallback for anything an older host did not send. `locale` and
    // `dark` are repeated here because QML needs them as plain properties.
    Q_PROPERTY(QVariantMap look READ look NOTIFY appearanceChanged)
    Q_PROPERTY(QString locale READ locale NOTIFY appearanceChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY appearanceChanged)
    Q_PROPERTY(QString uiSize READ uiSize NOTIFY appearanceChanged)

public:
    explicit Bridge(QObject* parent = nullptr);

    // Where the read-only batch record goes. Passed in by main.cpp from the
    // host's data directory rather than read from the environment here, so this
    // class has no hidden dependency on process state.
    void SetDataDir(const std::wstring& dataDir) { m_dataDir = dataDir; }

    // The host's look, as it sent it (initialize's `appearance`, then each
    // `appearance` notification). Empty on a host too old to send one, which is
    // why `look` staying empty has to mean "use the built-in palette".
    void ApplyAppearance(const QString& locale, bool dark, const QString& uiSize,
                         const QVariantMap& nativeLook);

    QString mode() const;
    QString phase() const;
    QString buffer() const;
    void setBuffer(const QString& text);
    QString blockingReason() const;
    int readyCount() const;
    int unchangedCount() const;
    int skippedCount() const;
    int errorCount() const;
    bool canExecute() const;
    int visibleCount() const;
    int totalCount() const;
    bool allSelected() const;
    QString blockedLine() const { return m_blockedLine; }
    QString filter() const;
    QString currentFolder() const { return m_folder; }
    QString pathForRow(int visibleRow) const;
    bool rowIsFolder(int visibleRow) const;
    bool hasUncommittedEdits() const;
    PreviewModel* previewModel();
    QVariantMap look() const { return m_look; }
    QString locale() const { return m_locale; }
    bool dark() const { return m_dark; }
    QString uiSize() const { return m_uiSize; }

    // ---- rule chain ----------------------------------------------------------
    // The chain, as the rule list holds it. The list renders one row per rule in
    // order; the detail editor edits the selected row. Truth lives here, the
    // widgets only read ruleAt() and write back through setRuleAt().
    Q_PROPERTY(RuleListModel* ruleModel READ ruleModel CONSTANT)
    Q_PROPERTY(int selectedRule READ selectedRule NOTIFY ruleChanged)
    Q_PROPERTY(int ruleCount READ ruleCount NOTIFY ruleChanged)
    // Case sensitivity is a flag on the whole batch, as PowerRename's checkbox is
    // (not per rule): one batch compares names one way.
    Q_PROPERTY(bool ruleCaseSensitive READ ruleCaseSensitive WRITE setRuleCaseSensitive
               NOTIFY ruleChanged)
    // The active case transform chip: 0 none, 1 lower, 2 upper, 3 capitalize,
    // 4 title. One at a time, because the core's flags are mutually exclusive in
    // practice (a batch is transformed once).
    Q_PROPERTY(int caseTransform READ caseTransform NOTIFY ruleChanged)

    RuleListModel* ruleModel() { return &m_ruleModel; }
    int selectedRule() const { return m_selectedRule; }
    int ruleCount() const { return static_cast<int>(m_chain.rules.size()); }
    bool ruleCaseSensitive() const;
    void setRuleCaseSensitive(bool caseSensitive);
    int caseTransform() const { return m_caseTransform; }

    // A rule as the detail editor shows it: scope (0 name, 1 extension, 2 full),
    // match (0 literal, 1 regex), occurrence (0 first, 1 all), the two strings,
    // and per-rule enabled. "error" is true when a regex pattern does not
    // compile; the editor turns the find box red.
    Q_INVOKABLE QVariantMap ruleAt(int index) const;
    Q_INVOKABLE void setRuleAt(int index, int scope, int match, const QString& find,
                               const QString& replace, bool enabled, bool matchAll);
    Q_INVOKABLE void selectRule(int index);
    Q_INVOKABLE int addRule();
    Q_INVOKABLE void duplicateRule(int index);
    Q_INVOKABLE void removeRule(int index);
    Q_INVOKABLE void moveRule(int from, int to);

    // The selected rule's regex does not compile: the find box turns red before
    // the user presses anything. A bad pattern surfaces as bad-rule rows anyway.
    Q_INVOKABLE bool ruleRegexInvalid() const;
    QString doneLine() const { return m_doneLine; }

    // ---- presets ---------------------------------------------------------------
    // Named chains stored in <dataDir>/presets.json. The first read seeds three
    // built-ins so the menu is never empty.
    Q_INVOKABLE QStringList presetNames() const;
    Q_INVOKABLE void loadPreset(const QString& name);
    Q_INVOKABLE bool savePreset(const QString& name);
    Q_INVOKABLE void deletePreset(const QString& name);

    // Called by the host when a `files` selection arrives.
    Q_INVOKABLE void setSelection(const QStringList& paths, bool recursive);
        // "Open the rename window in this folder": the folder's direct children
        // become the input set (EnumerateRequest::listContents). Kept separate from
        // setSelection so the two meanings — "these items" and "what is in here" —
        // cannot be confused at a call site.
        Q_INVOKABLE void setFolder(const QString& folder, bool recursive);
    Q_INVOKABLE void openFolder(const QString& folder);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void reloadFolder();
    // False when the current mode has unexecuted edits; the Widgets window then
    // asks the user and calls forceSwitchMode(). (The old QML path returned void
    // and silently ignored the click.)
    Q_INVOKABLE bool switchMode(const QString& mode);
    Q_INVOKABLE bool forceSwitchMode(const QString& mode);
    Q_INVOKABLE void setFilter(const QString& filter);
    Q_INVOKABLE void toggleRow(const QString& id, bool selected);
    Q_INVOKABLE void selectAll(bool selected);
    // A case transform over the whole batch: 0 none, 1 lower, 2 upper,
    // 3 capitalize, 4 title. Same flags the core already implements.
    Q_INVOKABLE void setCaseTransform(int which);

    // The single-rule shorthand kept for the wrapper's grab hook and the tests.
    // Replaces the whole chain with one rule. scope: 0 = name, 1 = extension,
    // 2 = full. match: 0 = literal, 1 = regex.
    Q_INVOKABLE void setSingleRule(int scope, int match, const QString& find,
                                   const QString& replace, bool enabled);
    Q_INVOKABLE bool apply();
    Q_INVOKABLE void reset();

signals:
    void changed();
    void countsChanged();
    void executed(int renamedCount, QString journalPath);
    void appearanceChanged();
    void ruleChanged();

private:
    bool SwitchMode(const QString& target, bool force);
    void BuildOilTable();
    void Refresh();
    void PublishRows();
    // Push m_chain/m_flags into the presenter and republish. Every chain edit
    // funnels here, so no mutation can forget the plan refresh.
    void SyncControlState();
    void RefreshRuleErrors();
    void LoadPresets() const;
    void StorePresets() const;

    rename_ui::Presenter m_presenter;
    PreviewModel m_previewModel;
    RuleListModel m_ruleModel;
    // The chain as the UI edits it. Empty-search rules are kept here (they are
    // placeholders mid-typing) and are harmless in the core: both Replace paths
    // return the source unchanged for an empty search.
    rename_core::RuleChain m_chain;
    // Batch-level flags the UI owns: case transform bits and case sensitivity.
    std::uint32_t m_flags = 0;
    int m_selectedRule = -1;
    int m_caseTransform = 0;          // 0 none, 1 lower, 2 upper, 3 capitalize, 4 title
    struct Preset
    {
        QString name;
        rename_core::RuleChain chain;
        std::uint32_t flags = 0;
    };
    mutable std::vector<Preset> m_presets;
    mutable bool m_presetsLoaded = false;
    std::vector<rename_win::CollectedItem> m_collected;
    std::wstring m_dataDir;
    QVariantMap m_look;
    QString m_locale = QStringLiteral("zh");
    bool m_dark = false;
    QString m_uiSize = QStringLiteral("standard");
    QString m_doneLine;
    // A selection that could not be enumerated (a missing root, an unreadable
    // folder). Kept apart from m_doneLine: a failure is not a finished batch and
    // must not get the "undo with Ctrl+Z" line.
    QString m_blockedLine;
        // The folder this batch works in, when it came from "open here" rather than
        // from a selection. Empty for a plain selection.
        QString m_folder;
    };

} // namespace rename_qml
