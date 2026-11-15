// vim_qt.cpp — Qt <-> vimcore glue. See vim_qt.h for the rules this follows.
#include "ui/vim_qt.h"

#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace rename_widgets {

namespace {

// Absolute character offset of a (line, col) pair in the widget's text. Lines are
// paragraphs, columns are positions inside the block.
int positionOf(QPlainTextEdit* edit, const vimcore::Cursor& cursor)
{
    const QTextDocument* doc = edit->document();
    const QTextBlock block = doc->findBlockByNumber(static_cast<int>(cursor.line));
    if (!block.isValid())
        return doc->characterCount() - 1;
    const int length = block.length() - 1; // block.length() counts the separator
    const int col = std::min(static_cast<int>(cursor.col), length);
    return block.position() + col;
}

vimcore::Cursor cursorOf(QPlainTextEdit* edit)
{
    const QTextCursor caret = edit->textCursor();
    return {static_cast<std::size_t>(caret.blockNumber()),
            static_cast<std::size_t>(caret.positionInBlock())};
}

QString toQString(const std::wstring& text)
{
    return QString::fromStdWString(text);
}

} // namespace

// ---- QtDocument -----------------------------------------------------------------

std::wstring QtDocument::text() const
{
    return m_edit->toPlainText().toStdWString();
}

vimcore::Cursor QtDocument::cursor() const
{
    return cursorOf(m_edit);
}

void QtDocument::apply(const std::vector<vimcore::TextEdit>& edits)
{
    if (edits.empty())
        return;

    // One atomic replace per edit, in reverse order so earlier offsets stay
    // valid while the document shrinks and grows.
    QTextCursor caret = m_edit->textCursor();
    for (auto it = edits.rbegin(); it != edits.rend(); ++it)
    {
        QTextCursor range = caret;
        range.setPosition(static_cast<int>(it->start));
        range.setPosition(static_cast<int>(it->end), QTextCursor::KeepAnchor);
        range.insertText(toQString(it->replacement));
    }
}

void QtDocument::setCursor(vimcore::Cursor cursor)
{
    QTextCursor caret = m_edit->textCursor();
    caret.setPosition(positionOf(m_edit, cursor));
    m_edit->setTextCursor(caret);
}

void QtDocument::setSelection(const vimcore::Selection& selection)
{
    if (!selection.active)
        return; // Normal mode: the core's setCursor already placed the caret

    QTextCursor caret = m_edit->textCursor();
    const int anchor = positionOf(m_edit, selection.anchor);
    int head = positionOf(m_edit, selection.head);
    if (selection.lineWise)
    {
        // V: the whole line, including its newline, reads as selected.
        const QTextBlock block = m_edit->document()->findBlockByNumber(
            static_cast<int>(selection.head.line));
        if (block.isValid())
            head = block.position() + block.length() - 1;
    }
    else if (head >= anchor)
        head += 1; // the char under the cursor is part of the visual range
    caret.setPosition(anchor);
    caret.setPosition(head, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(caret);
}

void QtDocument::setStatus(const wchar_t* status)
{
    m_status = status ? QString::fromWCharArray(status) : QString();
}

// ---- VimTextFilter ---------------------------------------------------------------

VimTextFilter::VimTextFilter(QPlainTextEdit* editor, QObject* parent)
    : QObject(parent)
    , m_edit(editor)
{
    m_document = new QtDocument(editor);
    m_editor = std::make_unique<vimcore::VimEditor>(*m_document);
    // The filter is installed permanently and gated by m_enabled: installing it
    // only when vim mode turns on would miss the first key press after the toggle.
    editor->installEventFilter(this);
}

void VimTextFilter::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (enabled)
        resync();
}

void VimTextFilter::resync()
{
    // A fresh core over the current text and caret. Undo history is per-session
    // by design: switching modes or reloading the buffer starts a new one.
    m_editor = std::make_unique<vimcore::VimEditor>(*m_document);
    emit statusChanged(statusText());
}

QString VimTextFilter::statusText() const
{
    return m_editor ? m_document->status() : QString();
}

bool VimTextFilter::insertMode() const
{
    return m_editor && m_editor->mode() == vimcore::Mode::Insert;
}

void VimTextFilter::feed(const vimcore::Key& key)
{
    m_editor->key(key);
    const QString status = m_document->status();
    static QString last;
    if (status != last)
    {
        last = status;
        emit statusChanged(status);
    }
}

bool VimTextFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_edit || !m_enabled || !m_editor)
        return QObject::eventFilter(watched, event);

    // Only key presses are ours. Everything else — IME composition, input method
    // queries, clipboard, drag and drop — goes straight to the widget.
    if (event->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);

    // Insert mode is native except for Esc: typing must behave exactly as it did
    // before vim mode existed, IME included.
    if (m_editor->mode() == vimcore::Mode::Insert)
    {
        if (key->key() != Qt::Key_Escape)
            return QObject::eventFilter(watched, event);
        feed(vimcore::Key{vimcore::KeyName::Esc, L'\0', 1, false});
        return true;
    }

    // Window-owned chords stay with the window: Ctrl+S runs the batch, Ctrl+Z/Y
    // are the host undo, Alt and F-keys belong to the shell.
    const Qt::KeyboardModifiers modifiers = key->modifiers();
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    if (modifiers.testFlag(Qt::AltModifier) || modifiers.testFlag(Qt::MetaModifier))
        return QObject::eventFilter(watched, event);
    if (ctrl && key->key() != Qt::Key_R)
        return QObject::eventFilter(watched, event);
    if (key->key() >= Qt::Key_F1 && key->key() <= Qt::Key_F35)
        return QObject::eventFilter(watched, event);

    vimcore::Key translated;
    bool have = true;
    switch (key->key())
    {
    case Qt::Key_Escape:    translated = {vimcore::KeyName::Esc, L'\0', 1, false}; break;
    case Qt::Key_Return:
    case Qt::Key_Enter:     translated = {vimcore::KeyName::Enter, L'\0', 1, false}; break;
    case Qt::Key_Backspace: translated = {vimcore::KeyName::Backspace, L'\0', 1, false}; break;
    case Qt::Key_Delete:    translated = {vimcore::KeyName::Delete, L'\0', 1, false}; break;
    case Qt::Key_Tab:       translated = {vimcore::KeyName::Tab, L'\0', 1, false}; break;
    case Qt::Key_Left:      translated = {vimcore::KeyName::Left, L'\0', 1, false}; break;
    case Qt::Key_Right:     translated = {vimcore::KeyName::Right, L'\0', 1, false}; break;
    case Qt::Key_Up:        translated = {vimcore::KeyName::Up, L'\0', 1, false}; break;
    case Qt::Key_Down:      translated = {vimcore::KeyName::Down, L'\0', 1, false}; break;
    case Qt::Key_Home:      translated = {vimcore::KeyName::Home, L'\0', 1, false}; break;
    case Qt::Key_End:       translated = {vimcore::KeyName::End, L'\0', 1, false}; break;
    case Qt::Key_PageUp:    translated = {vimcore::KeyName::PageUp, L'\0', 1, false}; break;
    case Qt::Key_PageDown:  translated = {vimcore::KeyName::PageDown, L'\0', 1, false}; break;
    case Qt::Key_R:
        if (!ctrl)
        {
            have = false;
            break;
        }
        translated = {vimcore::KeyName::Char, L'R', 1, true}; // Ctrl+R redo
        break;
    default:
    {
        const QString text = key->text();
        if (text.isEmpty() || key->text().at(0).isNull())
        {
            have = false;
            break;
        }
        translated = {vimcore::KeyName::Char, text.at(0).unicode(), 1, false};
        break;
    }
    }

    if (!have)
        return QObject::eventFilter(watched, event);

    feed(translated);
    return true;
}

} // namespace rename_widgets
