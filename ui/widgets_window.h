// widgets_window.h — the Qt Widgets rename window.
//
// The widgets are a view over rename_qml::Bridge; no rename decision lives here.
// This is the replacement for Main.qml: the same rule column, preview table and
// status footer, drawn with controls that do not need a QML engine or a scene
// graph.
#pragma once

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QHBoxLayout;
class QPlainTextEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTimer;
class QToolButton;
class QTabBar;

namespace rename_qml {
class Bridge;
}

namespace rename_widgets {

class VimTextFilter;
struct Theme;

class Window : public QMainWindow
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
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

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
    void ShowPathEditor(bool show);

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
    // ":q" / ":q!" from the vim command line: close the plugin window, refusing
    // while the buffer still has unapplied edits unless the quit is forced.
    void OnVimQuit(bool force);
    // Show a vim-style message ("E37: ...") in the command line under the editor
    // until the next key changes the status.
    void ShowVimMessage(const QString& message);
    // Mirror the vim core state into the toolbar toggle (mode letter) and the
    // command-line label under the text editor. Safe to call any time.
    void RefreshVimStatus();
    void RefreshCursorPosition();
    // Point the preview table at the row the vim cursor is on (oil-style
    // feedback: edit a line, see which item it belongs to).
    void HighlightPreviewRow(int line);

    rename_qml::Bridge* m_bridge = nullptr;
    VimTextFilter* m_vim = nullptr;
    int m_vimLine = 0;

    Theme* m_theme = nullptr;

    QTabBar* m_editorTabs = nullptr;

    QLineEdit* m_path = nullptr;
    QWidget* m_breadcrumb = nullptr;
    QHBoxLayout* m_breadcrumbLayout = nullptr;
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
    // Vim mode is a mode of the text editor, toggled from the global title bar.
    QToolButton* m_vimToggle = nullptr;
    // The vim command/status line under the text editor (":w"-style feedback).
    // Hidden entirely while vim mode is off.
    QLabel* m_vimCommandLine = nullptr;
    // A transient message ("E37: ...") shown instead of the core status until the
    // user presses another key.
    QString m_vimMessage;
    QLabel* m_cursorPosition = nullptr;

    QCheckBox* m_selectAll = nullptr;
    QLabel* m_originalHeader = nullptr;
    QLabel* m_targetHeader = nullptr;
    QComboBox* m_filter = nullptr;
    QTableView* m_table = nullptr;
    QStackedWidget* m_tableStack = nullptr;
    QLabel* m_empty = nullptr;

    QLabel* m_hint = nullptr;
    QLabel* m_readyBadge = nullptr;
    QLabel* m_conflictBadge = nullptr;
    QLabel* m_deleteBadge = nullptr;

    QPushButton* m_apply = nullptr;

    QTimer* m_ruleTimer = nullptr;
    QTimer* m_textTimer = nullptr;

    bool m_syncing = false;
    bool m_asking = false;
    bool m_listPendingG = false;
    bool m_listPendingD = false;
};

} // namespace rename_widgets
