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
class QListView;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTimer;
class QToolButton;

namespace rename_qml {
class Bridge;
}

namespace rename_widgets {

class VimTextFilter;
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
    void RefreshRuleList();
    void RefreshPresets();
    void SyncRuleSelection();
    void Retranslate();
    void SyncRuleFields();
    void SyncTextEditor();
    void SyncPath();

    void NavigateTo(const QString& folder);
    void GoUp();
    void ReloadFolder();
    void BrowseFolder();
    bool ConfirmDiscard();

    void PushRule();
    void RequestMode(const QString& target);
    void ConfirmAndApply();
    bool Ask(const QString& text, const QString& acceptText);

    QString T(const char* zhText, const char* enText) const;
    bool zh() const;
    void ApplyTitleBar();
    void RefreshVim();

    rename_qml::Bridge* m_bridge = nullptr;
    VimTextFilter* m_vim = nullptr;

    Theme* m_theme = nullptr;

    QLabel* m_title = nullptr;
    QToolButton* m_modeRules = nullptr;
    QToolButton* m_modeText = nullptr;

    QLineEdit* m_path = nullptr;
    QToolButton* m_up = nullptr;
    QToolButton* m_browse = nullptr;
    QToolButton* m_reload = nullptr;

    QStackedWidget* m_leftStack = nullptr;

    // The rule chain: list (master), row buttons, presets, and the detail editor
    // bound to the selected rule.
    QListView* m_ruleList = nullptr;
    QToolButton* m_ruleAdd = nullptr;
    QToolButton* m_ruleDup = nullptr;
    QToolButton* m_ruleUp = nullptr;
    QToolButton* m_ruleDown = nullptr;
    QToolButton* m_ruleRemove = nullptr;
    QComboBox* m_preset = nullptr;
    QToolButton* m_presetSave = nullptr;
    QToolButton* m_presetDelete = nullptr;

    QLineEdit* m_find = nullptr;
    QLineEdit* m_replace = nullptr;
    QCheckBox* m_regex = nullptr;
    QCheckBox* m_matchAll = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QComboBox* m_scope = nullptr;
    QLabel* m_scopeLabel = nullptr;
    QLabel* m_formatLabel = nullptr;
    QToolButton* m_caseButtons[4] = {nullptr, nullptr, nullptr, nullptr};

    QPlainTextEdit* m_text = nullptr;
    // Vim mode: the chip that toggles it and the badge that shows the core's
    // current mode. Off unless the user turned it on (persisted in settings).
    QToolButton* m_vimToggle = nullptr;
    QLabel* m_vimBadge = nullptr;

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
    bool m_asking = false;
};

} // namespace rename_widgets
