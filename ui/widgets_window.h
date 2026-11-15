// widgets_window.h — the Qt Widgets rename window.
//
// The widgets are a view over rename_qml::Bridge; no rename decision lives here.
// This is the replacement for Main.qml: the same rule column, preview table and
// status footer, drawn with controls that do not need a QML engine or a scene
// graph.
#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTimer;
class QToolButton;

namespace rename_qml {
class Bridge;
}

namespace rename_widgets {

struct Theme;

class Window : public QWidget
{
    Q_OBJECT

public:
    explicit Window(rename_qml::Bridge* bridge, QWidget* parent = nullptr);
    ~Window() override;

    // Re-read Bridge::look() and restyle the window after the host changed its
    // appearance.
    void ApplyAppearance();

    // RENAME_GRAB_WINDOW: render the window to a PNG, then quit. A diagnostic
    // hook used by the standalone wrapper, not a user feature.
    void GrabToFile(const QString& path);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void BuildUi();
    void ConnectBridge();

    void RefreshAll();
    void RefreshCounts();
    void RefreshEmptyState();
    void RefreshFooter();
    void RefreshMode();
    void SyncRuleFields();
    void SyncTextEditor();

    void PushRule();
    void RequestMode(const QString& target);
    void ConfirmAndApply();
    bool Ask(const QString& text, const QString& acceptText);

    QString T(const char* zhText, const char* enText) const;
    bool zh() const;
    void ApplyTitleBar();

    rename_qml::Bridge* m_bridge = nullptr;

    Theme* m_theme = nullptr;

    QToolButton* m_modeRules = nullptr;
    QToolButton* m_modeText = nullptr;

    QStackedWidget* m_leftStack = nullptr;

    QLineEdit* m_find = nullptr;
    QLineEdit* m_replace = nullptr;
    QCheckBox* m_regex = nullptr;
    QCheckBox* m_matchAll = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QComboBox* m_scope = nullptr;
    QToolButton* m_caseButtons[4] = {nullptr, nullptr, nullptr, nullptr};

    QPlainTextEdit* m_text = nullptr;

    QCheckBox* m_selectAll = nullptr;
    QLabel* m_originalHeader = nullptr;
    QLabel* m_targetHeader = nullptr;
    QComboBox* m_filter = nullptr;
    QTableView* m_table = nullptr;
    QStackedWidget* m_tableStack = nullptr;
    QLabel* m_empty = nullptr;

    QLabel* m_dot = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_hint = nullptr;

    QPushButton* m_apply = nullptr;

    QTimer* m_ruleTimer = nullptr;
    QTimer* m_textTimer = nullptr;

    bool m_syncing = false;
};

} // namespace rename_widgets
