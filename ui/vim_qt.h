// vim_qt.h — the Qt adapter for vimcore (text mode only).
//
// vimcore knows nothing about Qt: it edits a vimcore::Document. This file is
// that Document for a QPlainTextEdit, plus the key translation and the event
// filter that feed it.
//
// Deliberate design points:
//   * Insert mode is LEFT ALONE. Only Esc is intercepted there, so IME (Chinese
//     input), clipboard, drag-and-drop and the platform's own editing all behave
//     exactly as before. A vim mode that breaks typing is worse than none.
//   * Normal/Visual intercept keys except the ones that belong to the window
//     (Ctrl+S run, Ctrl+Z/Y host undo, Alt/F-key chords).
//   * vimcore keeps its own undo stack, so "u" and Ctrl+R never touch the
//     widget's undo history.
#pragma once

#include "vimcore/document.hpp"
#include "vimcore/editor.hpp"

#include <QObject>
#include <QString>
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
    void setEnabled(bool enabled);
    // Re-read the widget after something else changed the buffer (mode switch,
    // apply, external load).
    void resync();

    QString statusText() const;
    bool insertMode() const;

signals:
    // Emitted when the mode changes, so the window can show "-- NORMAL --".
    void statusChanged(const QString& status);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void feed(const vimcore::Key& key);

    QPlainTextEdit* m_edit = nullptr;
    QtDocument* m_document = nullptr;
    std::unique_ptr<vimcore::VimEditor> m_editor;
    bool m_enabled = false;
};

} // namespace rename_widgets
