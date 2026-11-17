// vim_qt.cpp — Qt <-> vimcore glue. See vim_qt.h for the rules this follows.
#include "ui/vim_qt.h"

#include <QKeyEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

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

// The last position of a line, including its paragraph separator, clamped to the
// document: what a linewise selection has to cover.
int lineEndPosition(QPlainTextEdit* edit, std::size_t line)
{
    const QTextDocument* doc = edit->document();
    const QTextBlock block = doc->findBlockByNumber(static_cast<int>(line));
    if (!block.isValid())
        return doc->characterCount() - 1;
    const int end = block.position() + block.length(); // past the separator
    return std::min(end, doc->characterCount() - 1);
}

int lineStartPosition(QPlainTextEdit* edit, std::size_t line)
{
    const QTextDocument* doc = edit->document();
    const QTextBlock block = doc->findBlockByNumber(static_cast<int>(line));
    return block.isValid() ? block.position() : 0;
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
    if (onCursorLine)
        onCursorLine(cursor.line);
}

void QtDocument::setSelection(const vimcore::Selection& selection)
{
    m_selection = selection;
    if (selection.blockWise) return;
    if (!selection.active)
        return; // Normal mode: the core's setCursor already placed the caret

    int anchor = positionOf(m_edit, selection.anchor);
    int head = positionOf(m_edit, selection.head);

    if (selection.lineWise)
    {
        // V: whole lines on both ends, not just the characters between the two
        // columns. Expanding only the head is what made V look like v.
        const int anchorLine = static_cast<int>(selection.anchor.line);
        const int headLine = static_cast<int>(selection.head.line);
        const int low = std::min(anchorLine, headLine);
        const int high = std::max(anchorLine, headLine);
        const int lowPos = lineStartPosition(m_edit, static_cast<std::size_t>(low));
        const int highPos = lineEndPosition(m_edit, static_cast<std::size_t>(high));
        // Keep the caret on the end the cursor is actually at.
        QTextCursor caret = m_edit->textCursor();
        if (headLine >= anchorLine)
        {
            caret.setPosition(lowPos);
            caret.setPosition(highPos, QTextCursor::KeepAnchor);
        }
        else
        {
            caret.setPosition(highPos);
            caret.setPosition(lowPos, QTextCursor::KeepAnchor);
        }
        m_edit->setTextCursor(caret);
        return;
    }

    // Character-wise: vim includes the character under BOTH ends, so the end
    // that is not the cursor's moves one further. A backward selection used to
    // lose the anchor character.
    QTextCursor caret = m_edit->textCursor();
    if (head >= anchor)
    {
        caret.setPosition(anchor);
        caret.setPosition(head + 1, QTextCursor::KeepAnchor); // the char under the head
    }
    else
    {
        caret.setPosition(anchor + 1);
        caret.setPosition(head, QTextCursor::KeepAnchor); // the char under the anchor
    }
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
    m_document->onCursorLine = [this](std::size_t line) {
        const int value = static_cast<int>(line);
        if (value != m_lastLine)
        {
            m_lastLine = value;
            emit cursorLineChanged(value);
        }
    };
    m_editor = std::make_unique<vimcore::VimEditor>(*m_document);
    // The filter is installed permanently and gated by m_enabled: installing it
    // only when vim mode turns on would miss the first key press after the toggle.
    editor->installEventFilter(this);
    editor->viewport()->installEventFilter(this);
    m_savedCursorWidth = std::max(editor->cursorWidth(), 1);

    connect(editor, &QPlainTextEdit::textChanged, this, [this] {
        // Text can change behind the core's back: native insert typing, IME
        // commits, paste, and the host's own Ctrl+Z undo. Keep the core's copy
        // current in every mode, or the next vim command works on stale text.
        if (m_enabled && m_editor && !m_selfEdit)
            m_editor->refreshFromDocument();
        updateBlockCaret();
    });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        updateBlockCaret();
        if (m_document && m_edit)
        {
            const int line = m_edit->textCursor().blockNumber();
            if (line != m_lastLine)
            {
                m_lastLine = line;
                emit cursorLineChanged(line);
            }
        }
    });
    connect(editor, &QObject::destroyed, this, [this] { m_edit = nullptr; });
}

void VimTextFilter::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
    {
        if (m_enabled && m_editor)
            emit statusChanged(statusText());
        applyModeUi();
        return;
    }
    m_enabled = enabled;
    if (enabled)
        resync();
    else
    {
        emit statusChanged(QString());
        // Leaving vim mode must hand the input method back: the previous mode may
        // have been Normal (IME off) and the widget has to accept typing again.
        applyModeUi();
    }
    updateBlockCaret();
}

void VimTextFilter::resync()
{
    // A fresh core over the current text and caret. Undo history is per-session
    // by design: switching modes or reloading the buffer starts a new one.
    m_editor = std::make_unique<vimcore::VimEditor>(*m_document);
    m_lastStatus.clear();
    updateViewportRows();
    emit statusChanged(statusText());
    applyModeUi();
    updateBlockCaret();
}

QString VimTextFilter::statusText() const
{
    return m_editor ? m_document->status() : QString();
}

void VimTextFilter::finishEdit()
{
    if (m_enabled && m_editor && (insertMode() || replaceMode()))
        feed(vimcore::makeKey(vimcore::KeyName::Esc));
}

bool VimTextFilter::insertMode() const
{
    return m_editor && m_editor->mode() == vimcore::Mode::Insert;
}

bool VimTextFilter::replaceMode() const
{
    return m_editor && m_editor->mode() == vimcore::Mode::Replace;
}

void VimTextFilter::feed(const vimcore::Key& key)
{
    m_selfEdit = true;
    m_editor->key(key);
    m_selfEdit = false;
    const QString status = m_document->status();
    if (status != m_lastStatus)
    {
        m_lastStatus = status;
        emit statusChanged(status);
        applyModeUi();
    }
    // ":q" / ":q!" reach the window through this signal; the core cannot close
    // anything itself.
    if (const vimcore::QuitRequest request = m_editor->takeQuitRequest();
        request != vimcore::QuitRequest::None)
        emit quitRequested(request == vimcore::QuitRequest::Force);
    updateBlockCaret();
}

void VimTextFilter::applyModeUi()
{
    // Only Insert accepts text from the input method. Every other mode is driven
    // by vim keys, so a composing IME (Chinese, Japanese, ...) must not be able
    // to commit anything into the widget: it would bypass the state machine and
    // appear in Normal mode as plain typing.
    const bool insert = m_enabled && insertMode();
    const bool replace = m_enabled && replaceMode();
    // With vim off the widget is an ordinary editor and keeps its input method;
    // with vim on only Insert accepts text from it.
    const bool acceptIme = !m_enabled || insert;
    if (m_edit)
        m_edit->setAttribute(Qt::WA_InputMethodEnabled, acceptIme);
    // The attribute alone does not detach an input context that is already
    // attached (which is exactly the case when the user switches to Normal mode
    // while a Chinese IME is active). reset() drops the half-typed composition
    // without committing it, update() asks the platform plugin to re-read
    // ImEnabled and enable/disable the context accordingly.
    if (QInputMethod* inputMethod = QGuiApplication::inputMethod())
    {
        if (!acceptIme)
            inputMethod->reset();
        inputMethod->update(Qt::ImEnabled);
    }
    emit modeChanged(insert, replace);
}

void VimTextFilter::updateViewportRows()
{
    if (!m_edit || !m_editor)
        return;
    const int lineSpacing = m_edit->fontMetrics().lineSpacing();
    const int height = m_edit->viewport()->height();
    // Before the first layout the viewport has no height; 20 rows is the same
    // default the core uses, so paging behaves sensibly either way.
    const int rows = (lineSpacing > 0 && height > 0)
        ? std::max(1, height / lineSpacing)
        : 20;
    m_editor->setViewportRows(static_cast<std::size_t>(rows));
}

void VimTextFilter::updateBlockCaret()
{
    if (!m_edit)
        return;
    if (!m_enabled || !m_editor)
    {
        if (m_blockCaret || m_edit->cursorWidth() == 0)
        {
            m_blockCaret = false;
            m_edit->setCursorWidth(m_savedCursorWidth);
            m_edit->setExtraSelections({});
        }
        return;
    }

    if (insertMode())
    {
        // Insert keeps the platform caret: a thin bar reads correctly while
        // typing and matches what IME composition expects.
        if (m_blockCaret || m_edit->cursorWidth() == 0)
        {
            m_blockCaret = false;
            m_edit->setCursorWidth(m_savedCursorWidth);
            m_edit->setExtraSelections({});
        }
        return;
    }

    const auto block = m_document->selection();
    if (m_editor->mode() == vimcore::Mode::VisualBlock)
    {
        QList<QTextEdit::ExtraSelection> ranges;
        for (auto line = std::min(block.anchor.line, block.head.line);
             line <= std::max(block.anchor.line, block.head.line); ++line)
        {
            QTextEdit::ExtraSelection range;
            range.cursor = QTextCursor(m_edit->document());
            range.cursor.setPosition(positionOf(m_edit, {line, std::min(block.anchor.col, block.head.col)}));
            range.cursor.setPosition(positionOf(m_edit, {line, std::max(block.anchor.col, block.head.col) + 1}), QTextCursor::KeepAnchor);
            range.format.setBackground(m_edit->palette().color(QPalette::Highlight));
            range.format.setForeground(m_edit->palette().color(QPalette::HighlightedText));
            ranges.append(range);
        }
        m_edit->setExtraSelections(ranges);
        m_edit->setCursorWidth(m_savedCursorWidth);
        m_blockCaret = true;
        return;
    }
    if (m_editor->mode() == vimcore::Mode::Visual || m_editor->mode() == vimcore::Mode::VisualLine)
    {
        m_edit->setExtraSelections({});
        m_edit->setCursorWidth(m_savedCursorWidth);
        m_blockCaret = false;
        return;
    }
    QTextEdit::ExtraSelection selection;
    selection.cursor = m_edit->textCursor();
    QTextCharFormat format;
    // Invert the character cell: the block takes the text colour and the glyph
    // takes the background, which is what a terminal vim cursor looks like.
    const QPalette palette = m_edit->palette();
    const QColor fill = palette.color(QPalette::Highlight);
    const QColor glyph = palette.color(QPalette::HighlightedText);
    format.setBackground(fill);
    format.setForeground(glyph);
    if (selection.cursor.atBlockEnd())
    {
        // Nothing under the cursor (empty line, or past the last char): fill the
        // rest of the line so the cursor is still visible.
        format.setProperty(QTextFormat::FullWidthSelection, true);
    }
    else
        selection.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
    selection.format = format;
    m_edit->setCursorWidth(0); // the native caret would show through the block
    m_edit->setExtraSelections({selection});
    m_blockCaret = true;
}

bool VimTextFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_edit && (!m_edit || watched != m_edit->viewport()))
        return QObject::eventFilter(watched, event);
    if (!m_enabled || !m_editor)
        return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::Resize)
    {
        updateViewportRows();
        updateBlockCaret();
        return QObject::eventFilter(watched, event);
    }
    // Belt and braces for the input method: even if a platform plugin keeps an
    // IME attached outside Insert mode, its commits and composition events stop
    // here. Only the buffer would change, never the vim state machine.
    if (event->type() == QEvent::InputMethod && !insertMode())
        return true;
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
    {
        updateBlockCaret();
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonRelease)
    {
        // The native editor may leave a selection or a stale block cursor when
        // the user clicks the whitespace after a line.  Normalize the click to
        // the actual QTextCursor position immediately, then rebuild vim from
        // that position.  This keeps the highlighted line, the core cursor and
        // the next j/k command on the same line.
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint clickPosition = mouse->position().toPoint();
        if (m_edit && mouse->button() == Qt::LeftButton)
        {
            QTextCursor caret = m_edit->textCursor();
            caret.clearSelection();
            m_edit->setTextCursor(caret);
        }
        if (m_edit)
        {
            QMetaObject::invokeMethod(this, [this, clickPosition] {
                if (!m_enabled || !m_edit)
                    return;
                // Resolve the same point after QPlainTextEdit has processed the
                // mouse event. This is important for whitespace and clicks below
                // the last glyph: textCursor() can still describe the old line.
                QTextCursor caret = m_edit->cursorForPosition(clickPosition);
                caret.clearSelection();
                m_edit->setTextCursor(caret);
                resync();
            }, Qt::QueuedConnection);
        }
        return QObject::eventFilter(watched, event);
    }

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
    // are the host undo, Alt and F-keys belong to the shell. The core owns a
    // small, explicit list of Ctrl chords instead.
    const Qt::KeyboardModifiers modifiers = key->modifiers();
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    if (modifiers.testFlag(Qt::AltModifier) || modifiers.testFlag(Qt::MetaModifier))
        return QObject::eventFilter(watched, event);
    if (ctrl)
    {
        switch (key->key())
        {
        case Qt::Key_Q:
        case Qt::Key_V:
            feed(vimcore::Key{vimcore::KeyName::Char, L'q', 1, true});
            return true;
        case Qt::Key_R: // redo
            feed(vimcore::Key{vimcore::KeyName::Char, L'R', 1, true});
            return true;
        case Qt::Key_A: // increment number
            feed(vimcore::Key{vimcore::KeyName::Char, L'a', 1, true});
            return true;
        case Qt::Key_X: // decrement number
            feed(vimcore::Key{vimcore::KeyName::Char, L'x', 1, true});
            return true;
        case Qt::Key_D: // half page down
            feed(vimcore::Key{vimcore::KeyName::Char, L'd', 1, true});
            return true;
        case Qt::Key_U: // half page up
            feed(vimcore::Key{vimcore::KeyName::Char, L'u', 1, true});
            return true;
        case Qt::Key_F: // full page down
            feed(vimcore::Key{vimcore::KeyName::Char, L'f', 1, true});
            return true;
        case Qt::Key_B: // full page up
            feed(vimcore::Key{vimcore::KeyName::Char, L'b', 1, true});
            return true;
        default:
            return QObject::eventFilter(watched, event);
        }
    }
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
    default:
    {
        // Printable characters (including 'r' and 'R'!) come through as text. A
        // key that is not one of the named ones and carries no text is left to
        // the widget.
        const QString text = key->text();
        if (text.isEmpty() || text.at(0).isNull())
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
