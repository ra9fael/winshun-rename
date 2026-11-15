// vim_qt.h — the Qt adapter for vimcore (text mode only).
//
// vimcore knows nothing about Qt: it edits a vimcore::Document. This file is
// that Document for a QPlainTextEdit, plus the key translation, the event
// filter that feeds it, and the vim-looking caret.
//
// Deliberate design points:
//   * Insert mode is LEFT ALONE. Only Esc is intercepted there, so IME (Chinese
//     input), clipboard, drag-and-drop and the platform's own editing all behave
//     exactly as before. A vim mode that breaks typing is worse than none.
//     Replace mode (R) is different by nature: vim overwrites characters, which
//     a plain QPlainTextEdit cannot do natively, so those keys go to the core
//     and the input method is switched off for the duration of the mode.
//   * Normal/Visual intercept keys except the ones that belong to the window
//     (Ctrl+S run, Ctrl+Z/Y host undo, Alt/F-key chords).
//   * vimcore keeps its own undo stack, so "u" and Ctrl+R never touch the
//     widget's undo history.
//   * The caret is drawn the vim way: a filled block in Normal/Visual, so the
//     character under the cursor is unambiguous with a monospace font.
#pragma once

#include "vimcore/document.hpp"
#include "vimcore/editor.hpp"

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QPlainTextEdit;

namespace rename_widgets {

// vimcore::Document over one QPlainTextEdit. Offsets are UTF-16 positions in
// QPlainTextEdit::toPlainText(), which uses '\n' between paragraphs — exactly
// the model vimcore expects, so no translation is needed.
class QtDocument final : public vimcore::Document
{
public:
    explicit QtDocument(QPlainTextEdit* edit) : m_edit(edit) {}

    std::wstring text() const override;
    vimcore::Cursor cursor() const override;
    void apply(const std::vector<vimcore::TextEdit>& edits) override;
    void setCursor(vimcore::Cursor cursor) override;
    void setSelection(const vimcore::Selection& selection) override;
    void setStatus(const wchar_t* status) override;

    // The status line the core last asked for ("-- NORMAL --"), for the badge.
    QString status() const { return m_status; }

    // Called with the cursor's line after every move, so the window can point
    // the preview table at the row being edited.
    std::function<void(std::size_t)> onCursorLine;

private:
    QPlainTextEdit* m_edit = nullptr;
    QString m_status;
};

// Installs itself as an event filter on the editor and translates Qt key events
// into vimcore::Key values.
class VimTextFilter : public QObject
{
    Q_OBJECT

public:
    explicit VimTextFilter(QPlainTextEdit* editor, QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    // Turning the filter on/off also rewinds the core to the widget's current
    // text and caret, so a stale copy can never overwrite what the user sees.
    // Calling setEnabled() with the current value is a no-op: RefreshAll() may
    // run after every buffer update and must not reset Insert mode.
    void setEnabled(bool enabled);
    // Re-read the widget after something else changed the buffer (mode switch,
    // apply, external load).
    void resync();

    QString statusText() const;
    bool insertMode() const;
    bool replaceMode() const;
    // The block cursor is an extra selection, so tests (and the theme code) can
    // ask whether it is currently shown.
    bool blockCaretVisible() const { return m_blockCaret; }

signals:
    // Emitted when the mode changes, so the window can show "-- NORMAL --".
    void statusChanged(const QString& status);
    // Emitted when the cursor moves to a different line (0-based).
    void cursorLineChanged(int line);
    // Emitted when the mode changes, after the caret/input-method state settled.
    void modeChanged(bool insert, bool replace);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void feed(const vimcore::Key& key);
    void applyModeUi();
    void updateBlockCaret();
    void updateViewportRows();

    QPlainTextEdit* m_edit = nullptr;
    QtDocument* m_document = nullptr;
    std::unique_ptr<vimcore::VimEditor> m_editor;
    bool m_enabled = false;
    bool m_selfEdit = false;
    QString m_lastStatus;

    int m_savedCursorWidth = 1;
    bool m_blockCaret = false;
    int m_lastLine = -1;
};

} // namespace rename_widgets
