// test_vim_window.cpp — the two window-level vim rules that no core test can see.
//
// Both bugs this file guards against were about the *window*, not the editing
// core, which is why vimcore and even the adapter suite were green:
//   1. Esc closed the window. A window-wide QShortcut for Esc is matched before
//      key delivery, so it never reached the vim filter. Esc must close the
//      window only when the vim-enabled text editor does not own it.
//   2. Typing a character in Insert mode flipped back to Normal. The text timer
//      pushes the buffer to the Bridge, Bridge::changed() runs RefreshAll(), and
//      RefreshAll() re-applied the vim setting — which rebuilt the core and put
//      it back in Normal. Re-applying a setting must be a no-op.
#include "ui/bridge.h"
#include "ui/vim_qt.h"
#include "ui/widgets_window.h"

#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QThread>
#include <QShortcut>
#include <QToolButton>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace vimwin_test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

inline void reportFailure(const char* file, int line, const char* expr)
{
    ++failures();
    std::printf("[ FAIL ] %s:%d: %s\n", file, line, expr);
}

inline int runAll()
{
    for (const TestCase& test : registry())
    {
        std::printf("[ RUN  ] %s\n", test.name);
        std::fflush(stdout);
        const int before = failures();
        test.fn();
        if (failures() == before)
            std::printf("[  OK  ] %s\n", test.name);
    }
    const int total = failures();
    if (total == 0)
        std::printf("[ PASS ] all %zu tests\n", registry().size());
    else
        std::printf("[ FAIL ] %d failed check(s) across %zu tests\n", total, registry().size());
    return total == 0 ? 0 : 1;
}

} // namespace vimwin_test

#define TEST_CASE_IMPL2(name, counter)                                                        \
    static void name##_##counter();                                                           \
    static const bool name##_##counter##_registered = [] {                                     \
        ::vimwin_test::registry().push_back({#name " #" #counter, name##_##counter});           \
        return true;                                                                           \
    }();                                                                                       \
    static void name##_##counter()

#define TEST_CASE_IMPL(name, counter) TEST_CASE_IMPL2(name, counter)
#define TEST_CASE(name) TEST_CASE_IMPL(name, __COUNTER__)

#define CHECK(expr)                                                                           \
    do {                                                                                      \
        if (!(expr))                                                                          \
            ::vimwin_test::reportFailure(__FILE__, __LINE__, #expr);                           \
    } while (false)

#define CHECK_EQ(actual, expected)                                                            \
    do {                                                                                      \
        const auto& a_ = (actual);                                                            \
        const auto& e_ = (expected);                                                          \
        if (!(a_ == e_))                                                                      \
            ::vimwin_test::reportFailure(__FILE__, __LINE__, #actual " == " #expected);        \
    } while (false)

namespace {

// One real window over a real Bridge, in text mode, with a couple of temp files
// so the editor has something to show.
struct Harness
{
    Harness()
    {
        QDir().mkpath(m_temp.path());
        for (const char* name : {"alpha.txt", "beta.txt"})
        {
            QFile file(m_temp.path() + QLatin1Char('/') + QLatin1String(name));
            if (file.open(QIODevice::WriteOnly))
                file.write("x");
        }
        bridge.SetDataDir(m_temp.path().toStdWString());
        bridge.setFolder(m_temp.path(), false);
        bridge.forceSwitchMode(QStringLiteral("text"));

        window = new rename_widgets::Window(&bridge);
        window->resize(880, 640);
        window->show();
        text = window->findChild<QPlainTextEdit*>();
        vim = window->findChild<rename_widgets::VimTextFilter*>();
        pump(120);
        if (text)
            text->setFocus(Qt::MouseFocusReason);
        pump(50);
    }

    ~Harness() { delete window; }

    void press(int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString& text_ = {})
    {
        if (!text)
            return;
        QKeyEvent event(QEvent::KeyPress, key, mods,
                        text_.isEmpty() && key < 0x100 ? QString(QChar(key)) : text_);
        QApplication::sendEvent(text, &event);
    }

    void type(const char* latin)
    {
        for (const char* p = latin; *p; ++p)
            press(static_cast<unsigned char>(*p));
    }

    // Let timers and queued slots run: the buffer sync is a 120 ms single-shot.
    static void pump(int ms)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(5);
        }
    }

    QString text1() const { return text ? text->toPlainText() : QString(); }
    bool insertMode() const { return vim && vim->insertMode(); }
    QString status() const { return vim ? vim->statusText() : QString(); }
    int cursorLine() const { return text ? text->textCursor().blockNumber() : -1; }

    // The preview table (right pane) as the user sees it.
    QTableView* table() const { return window ? window->findChild<QTableView*>() : nullptr; }
    QString previewTarget(int row)
    {
        auto* model = bridge.previewModel();
        return model->data(model->index(row, 0), rename_qml::PreviewModel::TargetRole).toString();
    }
    QString previewOriginal(int row)
    {
        auto* model = bridge.previewModel();
        return model->data(model->index(row, 0), rename_qml::PreviewModel::OriginalRole).toString();
    }
    int previewRows() { return bridge.previewModel()->rowCount(); }
    int previewCurrentRow() const
    {
        QTableView* view = table();
        return view ? view->currentIndex().row() : -1;
    }
    // Move the vim cursor to the start of a buffer line ("/1 name"), then past
    // the "/<id> " anchor so commands land on the name.
    void gotoNameStart(int line)
    {
        if (!text)
            return;
        QTextCursor caret = text->textCursor();
        caret.movePosition(QTextCursor::Start);
        for (int i = 0; i < line; ++i)
            caret.movePosition(QTextCursor::Down);
        text->setTextCursor(caret);
        pump(20);
        vim->resync();
        type("0");
        type("llll"); // past "<state> <id> " (y, space, digit, space)
    }

    QTemporaryDir m_temp;
    rename_qml::Bridge bridge;
    rename_widgets::Window* window = nullptr;
    QPlainTextEdit* text = nullptr;
    rename_widgets::VimTextFilter* vim = nullptr;
};

} // namespace

TEST_CASE(CtrlSInVimOpensRenameConfirmation)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    h.gotoNameStart(0);
    h.press(Qt::Key_Q, Qt::ControlModifier);
    h.type("jIprefix-");
    bool asked = false;
    QTimer closer;
    QObject::connect(&closer, &QTimer::timeout, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            asked = true;
            CHECK(box->defaultButton());
            CHECK(box->buttonRole(box->defaultButton()) == QMessageBox::AcceptRole);
            box->reject();
        }
    });
    closer.start(10);
    h.window->activateWindow();
    h.text->setFocus();
    Harness::pump(30);
    h.press(Qt::Key_S, Qt::ControlModifier);
    Harness::pump(30);
    CHECK(asked);
    CHECK(!h.insertMode());
    CHECK(h.previewTarget(0).contains(QStringLiteral("prefix-")));
    CHECK(h.previewTarget(1).contains(QStringLiteral("prefix-")));
}

TEST_CASE(NativeCtrlSOpensRenameConfirmation)
{
    if (QApplication::platformName() != QStringLiteral("windows"))
        return;
    Harness h;
    h.bridge.setVimEnabled(true);
    h.gotoNameStart(0);
    h.type("iprefix-");
    h.press(Qt::Key_Escape);
    QShortcut competingShortcut(QKeySequence(QStringLiteral("Ctrl+S")), h.window);
    bool asked = false;
    QTimer closer;
    QObject::connect(&closer, &QTimer::timeout, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            asked = true;
            box->reject();
        }
    });
    closer.start(10);
    h.window->activateWindow();
    const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD testThread = GetCurrentThreadId();
    AttachThreadInput(testThread, foregroundThread, TRUE);
    SetForegroundWindow(reinterpret_cast<HWND>(h.window->winId()));
    AttachThreadInput(testThread, foregroundThread, FALSE);
    h.text->setFocus();
    Harness::pump(200);
    CHECK_EQ(GetForegroundWindow(), reinterpret_cast<HWND>(h.window->winId()));
    if (GetForegroundWindow() != reinterpret_cast<HWND>(h.window->winId())) return;
    INPUT keys[4]{};
    for (auto& key : keys) key.type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_CONTROL;
    keys[1].ki.wVk = 'S';
    keys[2].ki.wVk = 'S';
    keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3].ki.wVk = VK_CONTROL;
    keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    const QList<QWidget*> targets{h.text, h.window->findChild<QTableView*>(),
                                  h.window->findChild<QLineEdit*>()};
    for (QWidget* target : targets)
    {
        CHECK(target);
        if (!target || !target->isVisible()) continue;
        target->setFocus();
        Harness::pump(30);
        asked = false;
        CHECK_EQ(SendInput(4, keys, sizeof(INPUT)), 4u);
        Harness::pump(250);
        CHECK(asked);
    }
}

// ---- bug 1: Esc must not close the window while vim edits the text -----------

TEST_CASE(EscInInsertLeavesInsertButKeepsTheWindow)
{
    Harness h;
    CHECK(h.text != nullptr);
    CHECK(h.vim != nullptr);
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    CHECK(h.vim->enabled());

    h.type("i");
    CHECK(h.insertMode());
    h.type("X");
    h.press(Qt::Key_Escape);

    CHECK(h.window->isVisible());      // the window is still there
    CHECK(!h.insertMode());            // Esc left Insert, as vim does
    CHECK_EQ(h.status(), QStringLiteral("-- NORMAL --"));
    CHECK(h.text1().contains(QLatin1Char('X')));
}

TEST_CASE(EscInNormalKeepsTheWindowOpen)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    h.press(Qt::Key_Escape);
    CHECK(h.window->isVisible());
    CHECK_EQ(h.status(), QStringLiteral("-- NORMAL --"));
}

TEST_CASE(EscWithVimOffStillClosesTheWindow)
{
    Harness h;
    CHECK(!h.vim->enabled());
    h.press(Qt::Key_Escape);
    CHECK(!h.window->isVisible()); // the original, non-vim behaviour is intact
}

TEST_CASE(EscWithFocusOutsideTheTextClosesTheWindow)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    // The find box owns the keyboard: Esc is the window's again. The event goes
    // to the focused widget, not to the text editor.
    auto* find = h.window->findChild<QLineEdit*>(QString(), Qt::FindDirectChildrenOnly);
    if (!find)
        find = h.window->findChild<QLineEdit*>();
    if (find)
    {
        find->setFocus(Qt::MouseFocusReason);
        Harness::pump(30);
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(find, &event);
        Harness::pump(30);
    }
    CHECK(!h.window->isVisible());
}

// ---- bug 2: a buffer sync must not drop Insert mode -------------------------

TEST_CASE(TypingInInsertKeepsInsertMode)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    h.type("i");
    CHECK(h.insertMode());
    h.type("hello");
    // The 120 ms text timer pushes the buffer to the Bridge, which runs
    // RefreshAll() -> RefreshVim() -> setVimEnabled(same value). That chain used
    // to rebuild the core and land in Normal after the first character.
    Harness::pump(320);

    CHECK(h.insertMode());
    CHECK_EQ(h.status(), QStringLiteral("-- INSERT --"));
    CHECK(h.text1().contains(QStringLiteral("hello")));
}

TEST_CASE(RepeatedToggleOfTheSameValueKeepsTheMode)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    h.type("i");
    CHECK(h.insertMode());

    for (int i = 0; i < 5; ++i)
        h.vim->setEnabled(true);

    CHECK(h.insertMode());
    h.vim->setEnabled(false);
    CHECK(!h.vim->enabled());
    h.vim->setEnabled(true);
    CHECK(!h.insertMode()); // re-enabling starts a fresh core in Normal
    CHECK_EQ(h.status(), QStringLiteral("-- NORMAL --"));
}

// ---- the core keeps up with native insert typing ---------------------------

TEST_CASE(NormalEditsAfterInsertSeeTheTypedText)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    h.text->setPlainText(QStringLiteral("ab"));
    h.text->moveCursor(QTextCursor::Start);
    h.vim->resync();
    Harness::pump(150);

    h.type("i");            // insert at column 0
    h.type("XY");           // native typing, core only learns via textChanged
    h.press(Qt::Key_Escape);
    Harness::pump(150);
    CHECK_EQ(h.text1(), QStringLiteral("XYab"));

    h.type("$");            // last char of the real text
    h.type("x");            // delete it
    CHECK_EQ(h.text1(), QStringLiteral("XYa"));
}

TEST_CASE(UndoRestoresTheWholeInsertSession)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    h.text->setPlainText(QStringLiteral("ab"));
    h.text->moveCursor(QTextCursor::Start);
    h.vim->resync();
    Harness::pump(150);

    h.type("i");
    h.type("XYZ");
    h.press(Qt::Key_Escape);
    Harness::pump(150);
    CHECK_EQ(h.text1(), QStringLiteral("XYZab"));

    h.type("u");
    CHECK_EQ(h.text1(), QStringLiteral("ab")); // one undo for the whole session
}

// ---- preview: the right table follows the text buffer ------------------------

TEST_CASE(PreviewListsTheFilesBeforeAnyVimKey)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    CHECK_EQ(h.previewRows(), 2);
    CHECK_EQ(h.previewOriginal(0), QStringLiteral("alpha.txt"));
    CHECK_EQ(h.previewTarget(0), QStringLiteral("alpha.txt"));
}

TEST_CASE(PreviewTargetUpdatesAfterANormalModeEdit)
{
    // The user's question: "how do I preview a change in vim mode?" — edit the
    // line, and the target column follows 120 ms later.
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    CHECK_EQ(h.text1(), QStringLiteral("y 1 alpha.txt\ny 2 beta.txt"));

    h.press(Qt::Key_0);
    h.type("llll");         // onto the name "alpha.txt", past "e 1 "
    h.type("c$");           // replace the whole name, keeping the state and id
    h.type("gamma.txt");
    h.press(Qt::Key_Escape);
    Harness::pump(300);     // the buffer sync is a 120 ms single shot

    CHECK_EQ(h.text1(), QStringLiteral("y 1 gamma.txt\ny 2 beta.txt"));
    CHECK_EQ(h.previewTarget(0), QStringLiteral("gamma.txt"));
    CHECK_EQ(h.previewTarget(1), QStringLiteral("beta.txt"));
}

TEST_CASE(PreviewRowFollowsTheVimCursorLine)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    CHECK_EQ(h.previewCurrentRow(), 0); // first line highlighted to start with

    h.type("j");
    Harness::pump(60);
    CHECK_EQ(h.previewCurrentRow(), 1); // "/2 beta.txt" -> the second row

    h.type("k");
    Harness::pump(60);
    CHECK_EQ(h.previewCurrentRow(), 0);
}

TEST_CASE(MouseClickOnLineWhitespaceKeepsVimNavigationInSync)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    CHECK(h.text != nullptr);
    if (!h.text)
        return;

    h.text->moveCursor(QTextCursor::Start);
    h.vim->resync();
    const int lineHeight = h.text->fontMetrics().lineSpacing();
    const QPoint point(h.text->viewport()->width() - 5, lineHeight + lineHeight / 2);
    QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(h.text->viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(h.text->viewport(), &release);
    Harness::pump(80);

    CHECK_EQ(h.cursorLine(), 1);
    h.type("j");
    CHECK_EQ(h.cursorLine(), 1);
    h.type("k");
    CHECK_EQ(h.cursorLine(), 0);
    h.type("l");
    CHECK_EQ(h.cursorLine(), 0);
}

TEST_CASE(CtrlAOnANameUpdatesThePreview)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    // Give the first name a number, then bump it with vim's Ctrl-A.
    h.gotoNameStart(0);
    h.type("c$");
    h.type("img01.png");
    h.press(Qt::Key_Escape);
    Harness::pump(300);
    CHECK_EQ(h.previewTarget(0), QStringLiteral("img01.png"));

    h.type("0llll");
    h.type("f1");                      // onto the digit
    h.press(Qt::Key_A, Qt::ControlModifier);
    Harness::pump(300);
    CHECK_EQ(h.text1(), QStringLiteral("y 1 img02.png\ny 2 beta.txt"));
    CHECK_EQ(h.previewTarget(0), QStringLiteral("img02.png"));
}

TEST_CASE(ReplaceModeOverwritesInsideTheWindow)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    h.gotoNameStart(0);
    h.press(Qt::Key_R, Qt::ShiftModifier, QStringLiteral("R"));
    CHECK_EQ(h.status(), QStringLiteral("-- REPLACE --"));
    h.type("ZZZ"); // overwrites "alp"
    h.press(Qt::Key_Escape);
    CHECK_EQ(h.status(), QStringLiteral("-- NORMAL --"));
    CHECK_EQ(h.text1(), QStringLiteral("y 1 ZZZha.txt\ny 2 beta.txt"));
    Harness::pump(300);
    CHECK_EQ(h.previewTarget(0), QStringLiteral("ZZZha.txt"));
}

TEST_CASE(TextStateLetterAndTableCheckboxStaySynchronized)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    h.text->setPlainText(QStringLiteral("n 1 alpha.txt\ny 2 beta.txt"));
    h.vim->resync();
    Harness::pump(200);
    CHECK_EQ(h.bridge.previewModel()->data(h.bridge.previewModel()->index(0, 0),
                                           Qt::CheckStateRole).toInt(), int(Qt::Unchecked));

    const QModelIndex first = h.bridge.previewModel()->index(0, 0);
    h.bridge.previewModel()->setData(first, Qt::Checked, Qt::CheckStateRole);
    Harness::pump(100);
    CHECK_EQ(h.text1(), QStringLiteral("y 1 alpha.txt\ny 2 beta.txt"));
}

TEST_CASE(TableVimDdMarksEntryForDelete)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    QTableView* table = h.table();
    CHECK(table != nullptr);
    if (!table) return;
    table->setFocus(Qt::MouseFocusReason);
    table->setCurrentIndex(h.bridge.previewModel()->index(0, 0));
    QKeyEvent d1(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
    QApplication::sendEvent(table, &d1);
    QKeyEvent d2(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
    QApplication::sendEvent(table, &d2);
    Harness::pump(120);
    // "dd" rewrites the row's state letter instead of removing the line, so the
    // delete stays visible and can be undone.
    CHECK_EQ(h.text1(), QStringLiteral("d 1 alpha.txt\ny 2 beta.txt"));
    CHECK_EQ(h.bridge.previewModel()->rowCount(), 2);
    CHECK(h.bridge.previewModel()->data(h.bridge.previewModel()->index(0, 0),
                                        rename_qml::PreviewModel::PendingDeleteRole).toBool());
}

TEST_CASE(TableVimURestoresPendingDelete)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    QTableView* table = h.table();
    CHECK(table != nullptr);
    if (!table) return;
    table->setFocus(Qt::MouseFocusReason);
    table->setCurrentIndex(h.bridge.previewModel()->index(0, 0));
    QKeyEvent first(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
    QKeyEvent second(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
    QApplication::sendEvent(table, &first);
    QApplication::sendEvent(table, &second);
    Harness::pump(80);
    QKeyEvent undo(QEvent::KeyPress, Qt::Key_U, Qt::NoModifier, QStringLiteral("u"));
    QApplication::sendEvent(table, &undo);
    Harness::pump(120);
    CHECK_EQ(h.text1(), QStringLiteral("y 1 alpha.txt\ny 2 beta.txt"));
    CHECK(!h.bridge.previewModel()->data(h.bridge.previewModel()->index(0, 0),
                                         rename_qml::PreviewModel::PendingDeleteRole).toBool());
}

TEST_CASE(VimOffLeavesTheTextBufferAlone)
{
    Harness h;
    CHECK(!h.vim->enabled());
    const QString before = h.text1();
    h.type("x"); // plain typing goes to the widget, vim does not consume it
    CHECK(h.text1() == before || h.text1().contains(QLatin1Char('x')));
    CHECK(!h.vim->blockCaretVisible());
}

// ---- toolbar toggle shows the mode letter ------------------------------------

QLabel* commandLine(const Harness& h)
{
    return h.window->findChild<QLabel*>(QStringLiteral("vimCommandLine"));
}

TEST_CASE(VimToggleShowsModeLetterAndCommandLineShowsTheCommand)
{
    Harness h;
    auto* toggle = h.window->findChild<QToolButton*>(QStringLiteral("vimToggle"));
    CHECK(toggle != nullptr);
    auto* line = commandLine(h);
    CHECK(line != nullptr);

    // Off: the label is "VIM" and the command line is hidden entirely.
    CHECK(!toggle->isChecked());
    CHECK_EQ(toggle->text(), QStringLiteral("VIM"));
    CHECK(!line->isVisible());

    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    CHECK(h.vim->enabled());
    CHECK_EQ(toggle->text(), QStringLiteral("N"));
    CHECK(line->isVisible());
    CHECK_EQ(line->text(), QStringLiteral("-- NORMAL --"));

    // Insert flips the letter; the command line keeps the mode status.
    h.type("i");
    CHECK(h.insertMode());
    CHECK_EQ(toggle->text(), QStringLiteral("I"));
    CHECK(line->isVisible());

    h.press(Qt::Key_Escape);
    CHECK_EQ(toggle->text(), QStringLiteral("N"));

    // Visual and Visual-line letters.
    h.type("v");
    CHECK_EQ(toggle->text(), QStringLiteral("v"));
    h.press(Qt::Key_Escape);
    h.type("V");
    CHECK_EQ(toggle->text(), QStringLiteral("V"));
    h.press(Qt::Key_Escape);

    // A ":" command shows in the command line as it is typed.
    h.type(":norm Ix");
    CHECK_EQ(toggle->text(), QStringLiteral("N")); // command mode is not a core Mode
    CHECK(line->isVisible());
    CHECK(line->text().startsWith(QLatin1Char(':')));
    CHECK(line->text().contains(QStringLiteral("norm")));
    h.press(Qt::Key_Escape); // abandon the command
}

TEST_CASE(VimToggleOffAgainRestoresLabelAndHidesCommandLine)
{
    Harness h;
    auto* toggle = h.window->findChild<QToolButton*>(QStringLiteral("vimToggle"));
    auto* line = commandLine(h);
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    CHECK_EQ(toggle->text(), QStringLiteral("N"));
    h.type("i");
    CHECK_EQ(toggle->text(), QStringLiteral("I"));
    h.bridge.setVimEnabled(false);
    Harness::pump(80);
    CHECK(!h.vim->enabled());
    CHECK_EQ(toggle->text(), QStringLiteral("VIM"));
    CHECK(!line->isVisible());
}

TEST_CASE(ToolBarHasNoTitleLabel)
{
    Harness h;
    // The "批量重命名" text label is gone from the toolbar; the window title
    // (taskbar/caption) still carries the name.
    CHECK_EQ(h.window->windowTitle(), QStringLiteral("批量重命名"));
    for (QLabel* label : h.window->findChildren<QLabel*>())
        CHECK(label->text() != QStringLiteral("批量重命名"));
}

TEST_CASE(PreviewStatusColumnShowsStatusNotTheFileName)
{
    Harness h;
    auto* model = h.bridge.previewModel();
    CHECK(model->rowCount() >= 2);
    for (int row = 0; row < model->rowCount(); ++row)
    {
        const QString status = model->data(model->index(row, 3), Qt::DisplayRole).toString();
        // Before the fix this column returned the target leaf (then the view
        // elided it), which is exactly what the user saw.
        CHECK(status != model->data(model->index(row, 2), rename_qml::PreviewModel::TargetRole).toString()
              || model->data(model->index(row, 2), rename_qml::PreviewModel::TargetRole).toString().isEmpty());
        CHECK(!status.isEmpty());
    }
    // The target column still shows the file name.
    const QString target = model->data(model->index(0, 2), Qt::DisplayRole).toString();
    CHECK_EQ(target, model->data(model->index(0, 2), rename_qml::PreviewModel::TargetRole).toString());
}

TEST_CASE(UncheckedRowShowsNotSelectedInTheStatusColumn)
{
    Harness h;
    auto* model = h.bridge.previewModel();
    CHECK(model->rowCount() >= 2);

    h.bridge.toggleRow(model->data(model->index(0, 0), rename_qml::PreviewModel::IdRole).toString(), false);
    Harness::pump(60);

    const QString unchecked = model->data(model->index(0, 3), Qt::DisplayRole).toString();
    CHECK(unchecked == QStringLiteral("未选") || unchecked == QStringLiteral("Not selected"));
    // A checked row keeps its real status.
    const QString checked = model->data(model->index(1, 3), Qt::DisplayRole).toString();
    CHECK(checked != unchecked);
}

TEST_CASE(UncheckedRowsAreLeftOutOfTheRenameCount)
{
    Harness h;
    auto* model = h.bridge.previewModel();
    const QString readyBefore = QString::number(h.bridge.readyCount());
    Q_UNUSED(readyBefore);
    // Uncheck every row: nothing is left to rename, so the batch is empty.
    for (int row = 0; row < model->rowCount(); ++row)
        h.bridge.toggleRow(model->data(model->index(row, 0), rename_qml::PreviewModel::IdRole).toString(), false);
    Harness::pump(60);
    CHECK_EQ(h.bridge.readyCount(), 0);
    CHECK(!h.bridge.canExecute());
}

TEST_CASE(TableRevertRowRestoresNameAndSyncsText)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    // Rename through the buffer, then undo that one row through the bridge the
    // table's context menu calls.
    h.text->setPlainText(QStringLiteral("y 1 gamma.txt\ny 2 beta.txt"));
    h.vim->resync();
    Harness::pump(200);
    CHECK_EQ(h.previewTarget(0), QStringLiteral("gamma.txt"));

    auto* model = h.bridge.previewModel();
    const QString id = model->data(model->index(0, 0), rename_qml::PreviewModel::IdRole).toString();
    CHECK(h.bridge.revertRow(id));
    Harness::pump(120);

    CHECK_EQ(h.text1(), QStringLiteral("y 1 alpha.txt\ny 2 beta.txt"));
    CHECK_EQ(h.previewTarget(0), QStringLiteral("alpha.txt"));
}

TEST_CASE(TableRevertAllEditsKeepsSelection)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);

    h.text->setPlainText(QStringLiteral("y 1 gamma.txt\nn 2 delta.txt"));
    h.vim->resync();
    Harness::pump(200);

    CHECK(h.bridge.revertAllEdits());
    Harness::pump(120);

    // Names go back to the on-disk ones; the "n" the user set is kept.
    CHECK_EQ(h.text1(), QStringLiteral("y 1 alpha.txt\nn 2 beta.txt"));
}

TEST_CASE(CtrlQReplaceAcrossRows)
{
    // Ctrl+Q + r: replacing the state letter of every row in the block is the
    // realistic use (mark several rows with one keystroke).
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    h.text->setPlainText(QStringLiteral("y 1 aaaa.txt\ny 2 bbbb.txt\ny 3 cccc.txt"));
    h.vim->resync();
    Harness::pump(250);
    CHECK_EQ(h.text1(), QStringLiteral("y 1 aaaa.txt\ny 2 bbbb.txt\ny 3 cccc.txt"));

    h.text->setFocus();
    h.press(Qt::Key_Q, Qt::ControlModifier);
    h.type("jjrn");
    Harness::pump(150);
    CHECK_EQ(h.text1(), QStringLiteral("n 1 aaaa.txt\nn 2 bbbb.txt\nn 3 cccc.txt"));
    CHECK_EQ(h.bridge.readyCount(), 0); // every row is skipped now
}

TEST_CASE(VimQuitClosesTheWindowWhenNothingIsPending)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    CHECK_EQ(h.bridge.readyCount(), 0);
    CHECK_EQ(h.bridge.deleteCount(), 0);

    h.text->setFocus();
    h.type(":q");
    h.press(Qt::Key_Return);
    Harness::pump(250);
    CHECK(!h.window->isVisible()); // :q closed the plugin window
}

TEST_CASE(VimQuitRefusesPendingEditsUnlessForced)
{
    Harness h;
    h.bridge.setVimEnabled(true);
    Harness::pump(80);
    // An unapplied rename: this is what :q must refuse to throw away.
    h.text->setPlainText(QStringLiteral("y 1 gamma.txt\ny 2 beta.txt"));
    h.vim->resync();
    Harness::pump(250);
    CHECK(h.bridge.readyCount() > 0);

    h.text->setFocus();
    h.type(":q");
    h.press(Qt::Key_Return);
    Harness::pump(150);
    CHECK(h.window->isVisible()); // still open
    bool sawE37 = false;
    for (QLabel* label : h.window->findChildren<QLabel*>())
        if (label->isVisible() && label->text().contains(QStringLiteral("E37")))
            sawE37 = true;
    CHECK(sawE37);

    h.type(":q!");
    h.press(Qt::Key_Return);
    Harness::pump(250);
    CHECK(!h.window->isVisible());          // :q! closed it
    CHECK_EQ(h.bridge.readyCount(), 0);     // and dropped the pending edits
    CHECK_EQ(h.text1(), QStringLiteral("y 1 alpha.txt\ny 2 beta.txt"));
}

TEST_CASE(GoingUpKeepsTheTextTab)
{
    // Reported: from the text tab, "up one level" dropped the user back on the
    // rule tab. Navigation is a new input set, not a new task, so the tab stays
    // and the buffer is rebuilt from the folder that is now open.
    QTemporaryDir temp;
    const QString root = temp.path();
    QDir().mkpath(root + QStringLiteral("/sub"));
    QFile alpha(root + QStringLiteral("/alpha.txt"));
    QFile beta(root + QStringLiteral("/sub/beta.txt"));
    CHECK(alpha.open(QIODevice::WriteOnly));
    CHECK(beta.open(QIODevice::WriteOnly));
    alpha.close();
    beta.close();

    rename_qml::Bridge bridge;
    bridge.SetDataDir(root.toStdWString());
    bridge.setFolder(root + QStringLiteral("/sub"), false);
    CHECK(bridge.forceSwitchMode(QStringLiteral("text")));
    CHECK_EQ(bridge.mode(), QStringLiteral("text"));
    CHECK(bridge.buffer().contains(QStringLiteral("beta.txt")));

    bridge.goUp();
    CHECK_EQ(bridge.mode(), QStringLiteral("text"));
    CHECK(bridge.buffer().contains(QStringLiteral("alpha.txt")));
    CHECK(!bridge.buffer().contains(QStringLiteral("beta.txt")));
}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return vimwin_test::runAll();
}
