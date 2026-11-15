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
    void setColors(const QColor& text, const QColor& subtext, const QColor& faint, const QColor& danger);

signals:
    // Emitted when the user toggles a checkbox, so the bridge can push the
    // change back into the presenter and refresh the counts.
    void selectionChanged(const QString& id, bool selected);

private:
    QString targetText(const rename_ui::Row& row) const;

    std::vector<rename_ui::Row> m_rows;
    QString m_locale = QStringLiteral("zh");
    QColor m_text;
    QColor m_subtext;
    QColor m_faint;
    QColor m_danger;
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
    int errorCount() const;
    bool canExecute() const;
    int visibleCount() const;
    int totalCount() const;
    bool allSelected() const;
    bool ruleRegexInvalid() const;
    QString doneLine() const { return m_doneLine; }
    QString blockedLine() const { return m_blockedLine; }
    QString filter() const;
    PreviewModel* previewModel();
    QVariantMap look() const { return m_look; }
    QString locale() const { return m_locale; }
    bool dark() const { return m_dark; }
    QString uiSize() const { return m_uiSize; }

    // The rule as the window's column holds it. The QML side binds its inputs to
    // these, so a rule set from outside shows up in the form instead of leaving
    // blank boxes beside a preview that has clearly used a rule.
    Q_PROPERTY(QString ruleFind READ ruleFind NOTIFY ruleChanged)
    Q_PROPERTY(QString ruleReplace READ ruleReplace NOTIFY ruleChanged)
    Q_PROPERTY(int ruleScope READ ruleScope NOTIFY ruleChanged)
    Q_PROPERTY(int ruleMatch READ ruleMatch NOTIFY ruleChanged)
    Q_PROPERTY(bool ruleEnabled READ ruleEnabled NOTIFY ruleChanged)
    Q_PROPERTY(bool ruleMatchAll READ ruleMatchAll NOTIFY ruleChanged)
    Q_PROPERTY(bool ruleCaseSensitive READ ruleCaseSensitive NOTIFY ruleChanged)
    Q_PROPERTY(int caseTransform READ caseTransform NOTIFY ruleChanged)

    QString ruleFind() const { return m_ruleFind; }
    QString ruleReplace() const { return m_ruleReplace; }
    int ruleScope() const { return m_ruleScope; }
    int ruleMatch() const { return m_ruleMatch; }
    bool ruleEnabled() const { return m_ruleEnabled; }
    bool ruleMatchAll() const { return m_ruleMatchAll; }
    bool ruleCaseSensitive() const { return m_ruleCaseSensitive; }
    // The active case transform chip: 0 none, 1 lower, 2 upper, 3 capitalize,
    // 4 title. One at a time, because the core's flags are mutually exclusive in
    // practice (a batch is transformed once).
    int caseTransform() const { return m_caseTransform; }

    // The full rule form, as the window's column submits it. `scope`: 0 name,
    // 1 extension, 2 full name. `match`: 0 literal, 1 regex.
    Q_INVOKABLE void setRule(int scope, int match, const QString& find,
                             const QString& replace, bool enabled,
                             bool matchAll, bool caseSensitive);
    // "Apply to": 0 name, 1 extension, 2 full name. Kept separate from setRule so
    // pressing one of the three square buttons does not have to restate the
    // find/replace text the fields already hold.
    Q_INVOKABLE void setScope(int scope);

    // Called by the host when a `files` selection arrives.
    Q_INVOKABLE void setSelection(const QStringList& paths, bool recursive);
        // "Open the rename window in this folder": the folder's direct children
        // become the input set (EnumerateRequest::listContents). Kept separate from
        // setSelection so the two meanings — "these items" and "what is in here" —
        // cannot be confused at a call site.
        Q_INVOKABLE void setFolder(const QString& folder, bool recursive);
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
    // scope: 0 = name, 1 = extension, 2 = full. match: 0 = literal, 1 = regex.
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

    rename_ui::Presenter m_presenter;
    PreviewModel m_previewModel;
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

        // The single rule as the row holds it. Kept here (not only in the QML inputs)
        // so the row can be filled from outside and so the four values survive a
        // mode switch. Only the two strings are text the user edits; the rest are
        // choices.
        QString m_ruleFind;
        QString m_ruleReplace;
        int m_ruleScope = 0;      // 0 name, 1 extension, 2 full
        int m_ruleMatch = 0;      // 0 literal, 1 regex
        bool m_ruleEnabled = true;
        bool m_ruleMatchAll = true;      // replace every occurrence, as PowerRename's box
        bool m_ruleCaseSensitive = false; // Windows names are what they are: literal by default
        int m_caseTransform = 0;          // 0 none, 1 lower, 2 upper, 3 capitalize, 4 title
    };

} // namespace rename_qml
