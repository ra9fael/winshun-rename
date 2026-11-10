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
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const std::vector<rename_ui::Row>& rows);
    std::vector<rename_ui::Row> rows() const { return m_rows; }

signals:
    // Emitted when the user toggles a checkbox, so the bridge can push the
    // change back into the presenter and refresh the counts.
    void selectionChanged(const QString& id, bool selected);

private:
    std::vector<rename_ui::Row> m_rows;
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
    Q_PROPERTY(QString doneLine READ doneLine NOTIFY changed)
    Q_PROPERTY(QAbstractListModel* previewModel READ previewModel CONSTANT)

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
    QString doneLine() const { return m_doneLine; }
    QAbstractListModel* previewModel();
    QVariantMap look() const { return m_look; }
    QString locale() const { return m_locale; }
    bool dark() const { return m_dark; }
    QString uiSize() const { return m_uiSize; }

    // Called by the host when a `files` selection arrives.
    Q_INVOKABLE void setSelection(const QStringList& paths, bool recursive);
    Q_INVOKABLE void switchMode(const QString& mode);
    Q_INVOKABLE void setFilter(const QString& filter);
    Q_INVOKABLE void toggleRow(const QString& id, bool selected);
    Q_INVOKABLE void selectAll(bool selected);

    // The single-rule editor used by Main.qml until the chain editor lands in M5.
    // scope: 0 = name, 1 = extension, 2 = full. match: 0 = literal, 1 = regex.
    // One-way on purpose: the fields are the rule's only source, so echoing them
    // back would need a second source of truth for the same two strings.
    Q_INVOKABLE void setSingleRule(int scope, int match, const QString& find,
                                   const QString& replace, bool enabled);

    Q_INVOKABLE bool apply();
    Q_INVOKABLE void reset();

signals:
    void changed();
    void countsChanged();
    void executed(int renamedCount, QString journalPath);
    void appearanceChanged();

private:
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
};

} // namespace rename_qml
