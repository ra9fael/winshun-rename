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
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QThread>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

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

    QTemporaryDir m_temp;
    rename_qml::Bridge bridge;
    rename_widgets::Window* window = nullptr;
    QPlainTextEdit* text = nullptr;
    rename_widgets::VimTextFilter* vim = nullptr;
};

} // namespace

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
    auto* find = h.window->findChild<QLineEdit*>();
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

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return vimwin_test::runAll();
}
