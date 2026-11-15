// test_vim_qt.cpp — the Qt adapter, exercised with real key events.
//
// vimcore itself is tested without Qt (vendor/vimcore/tests). This file covers
// the part only Qt can get wrong: translating QKeyEvent into vimcore::Key,
// mapping offsets into QPlainTextEdit, and leaving Insert mode native so IME
// input is untouched.
#include "ui/vim_qt.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTextCursor>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace vimqt_test {

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

} // namespace vimqt_test

#define TEST_CASE_IMPL2(name, counter)                                                        \
    static void name##_##counter();                                                           \
    static const bool name##_##counter##_registered = [] {                                     \
        ::vimqt_test::registry().push_back({#name " #" #counter, name##_##counter});           \
        return true;                                                                           \
    }();                                                                                       \
    static void name##_##counter()

#define TEST_CASE_IMPL(name, counter) TEST_CASE_IMPL2(name, counter)
#define TEST_CASE(name) TEST_CASE_IMPL(name, __COUNTER__)

#define CHECK(expr)                                                                           \
    do {                                                                                      \
        if (!(expr))                                                                          \
            ::vimqt_test::reportFailure(__FILE__, __LINE__, #expr);                           \
    } while (false)

#define CHECK_EQ(actual, expected)                                                            \
    do {                                                                                      \
        const auto& a_ = (actual);                                                            \
        const auto& e_ = (expected);                                                          \
        if (!(a_ == e_))                                                                      \
            ::vimqt_test::reportFailure(__FILE__, __LINE__, #actual " == " #expected);        \
    } while (false)

using rename_widgets::VimTextFilter;

namespace {

// Records whether a key press reached the widget at all. Installed before the
// vim filter, so it only sees events the filter chose not to consume.
class SpyFilter : public QObject
{
public:
    bool sawKeyPress = false;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress)
            sawKeyPress = true;
        return false;
    }
};

// One editor + filter per test, no window: the widget is not shown, so no
// desktop, DPI or theme is involved.
struct Fixture
{
    explicit Fixture(const QString& initial, bool vim = true)
    {
        edit.setPlainText(initial);
        edit.moveCursor(QTextCursor::Start);
        // A QPlainTextEdit only inserts typed text when it is the active, focused
        // widget. Offscreen still needs an explicit active window, otherwise the
        // key event reaches the widget but the editor never commits text.
        edit.resize(400, 300);
        edit.show();
        edit.activateWindow();
        edit.setFocus(Qt::MouseFocusReason);
        edit.installEventFilter(&spy);            // sees only what the filter passes on
        filter = new VimTextFilter(&edit, nullptr);
        filter->setEnabled(vim);
    }

    void press(int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString& text = {})
    {
        spy.sawKeyPress = false;
        QKeyEvent event(QEvent::KeyPress, key, mods, text.isEmpty() && key < 0x100
                                                          ? QString(QChar(key)) : text);
        QApplication::sendEvent(&edit, &event);
    }

    void type(const char* latin)
    {
        for (const char* p = latin; *p; ++p)
            press(static_cast<unsigned char>(*p));
    }

    QString text() const { return edit.toPlainText(); }
    QTextCursor caret() const { return edit.textCursor(); }

    QPlainTextEdit edit;
    SpyFilter spy;
    VimTextFilter* filter = nullptr;
};

} // namespace

static void expectText(const Fixture& f, const char* expected)
{
    const QString actual = f.text();
    const QString want = QString::fromUtf8(expected);
    if (actual != want)
    {
        std::printf("[ FAIL ] text was \"%s\", expected \"%s\"\n", qPrintable(actual),
                    qPrintable(want));
        ++vimqt_test::failures();
    }
}

// ---- gating -----------------------------------------------------------------

TEST_CASE(DisabledByDefaultLeavesTypingAlone)
{
    Fixture f(QStringLiteral("abc"), /*vim=*/false);
    f.type("x");
    // Without vim mode "x" is typed at the caret (position 0), not "delete
    // under cursor".
    expectText(f, "xabc");
}

TEST_CASE(EnablingVimInterpretsKeys)
{
    Fixture f(QStringLiteral("abc"));
    CHECK(f.filter->enabled());
    f.type("x");
    CHECK_EQ(f.text(), QStringLiteral("bc")); // x deleted 'a', vim style
}

TEST_CASE(DisablingVimRestoresNativeTyping)
{
    Fixture f(QStringLiteral("abc"));
    f.filter->setEnabled(false);
    f.type("z");
    expectText(f, "zabc");
}

// ---- modes through real key events ------------------------------------------

TEST_CASE(InsertModeAndEscape)
{
    Fixture f(QStringLiteral("abc"));
    f.type("i");
    CHECK(f.filter->insertMode());
    f.type("X");
    CHECK_EQ(f.text(), QStringLiteral("Xabc"));
    // Esc leaves insert; vim puts the caret on the last typed char, so a
    // following x deletes that X, not a stale offset.
    f.press(Qt::Key_Escape);
    CHECK(!f.filter->insertMode());
    f.type("x");
    CHECK_EQ(f.text(), QStringLiteral("abc"));
}

TEST_CASE(InsertModeTypingIsNativeIncludingSpaces)
{
    Fixture f(QStringLiteral("ab"));
    f.type("i");
    f.type("hello world"); // spaces must reach the widget untouched
    CHECK_EQ(f.text(), QStringLiteral("hello worldab"));
}

TEST_CASE(NormalMotionAndDeleteWord)
{
    Fixture f(QStringLiteral("foo bar"));
    f.type("w");
    CHECK_EQ(f.caret().positionInBlock(), 4);
    f.type("dw");
    CHECK_EQ(f.text(), QStringLiteral("foo "));
}

TEST_CASE(DeleteLineAndUndo)
{
    Fixture f(QStringLiteral("one\ntwo\nthree"));
    f.type("dd");
    CHECK_EQ(f.text(), QStringLiteral("two\nthree"));
    f.type("u");
    CHECK_EQ(f.text(), QStringLiteral("one\ntwo\nthree"));
}

TEST_CASE(RedoWithCtrlR)
{
    Fixture f(QStringLiteral("one\ntwo"));
    f.type("dd");
    f.type("u");
    // Ctrl+R is the one Ctrl chord the filter consumes.
    f.press(Qt::Key_R, Qt::ControlModifier);
    CHECK_EQ(f.text(), QStringLiteral("two"));
}

TEST_CASE(VisualSelectionDeletes)
{
    Fixture f(QStringLiteral("abcdef"));
    f.type("vl");
    f.type("x");
    // v then l selects "ab"; x removes them.
    CHECK_EQ(f.text(), QStringLiteral("cdef"));
}

TEST_CASE(CaretPastFieldStartIsSafe)
{
    Fixture f(QStringLiteral("abc"));
    f.type("0");
    f.type("h"); // already at column 0: nothing to do, no crash
    CHECK_EQ(f.text(), QStringLiteral("abc"));
    CHECK_EQ(f.caret().positionInBlock(), 0);
}

// ---- window-owned chords pass through ---------------------------------------

TEST_CASE(CtrlSIsNotSwallowed)
{
    Fixture f(QStringLiteral("abc"));
    // Ctrl+S must reach the window shortcut (Rename), not the editor core.
    f.press(Qt::Key_S, Qt::ControlModifier, QStringLiteral("s"));
    CHECK(f.spy.sawKeyPress);
    CHECK_EQ(f.text(), QStringLiteral("abc"));
}

TEST_CASE(AltChordPassesThrough)
{
    Fixture f(QStringLiteral("abc"));
    f.press(Qt::Key_Left, Qt::AltModifier);
    CHECK(f.spy.sawKeyPress);
}

// ---- status badge -----------------------------------------------------------

TEST_CASE(StatusTextTracksMode)
{
    Fixture f(QStringLiteral("abc"));
    CHECK_EQ(f.filter->statusText(), QStringLiteral("-- NORMAL --"));
    f.type("i");
    CHECK_EQ(f.filter->statusText(), QStringLiteral("-- INSERT --"));
    f.press(Qt::Key_Escape);
    CHECK_EQ(f.filter->statusText(), QStringLiteral("-- NORMAL --"));
    f.type("v");
    CHECK_EQ(f.filter->statusText(), QStringLiteral("-- VISUAL --"));
}

// ---- resync after the buffer is replaced externally -------------------------

TEST_CASE(ResyncTakesTheNewText)
{
    Fixture f(QStringLiteral("old"));
    f.edit.setPlainText(QStringLiteral("brand new text"));
    f.edit.moveCursor(QTextCursor::Start);
    f.filter->resync();
    f.type("x");
    CHECK_EQ(f.text(), QStringLiteral("rand new text"));
}

// ---- Replace mode (R) --------------------------------------------------------

TEST_CASE(CapitalRNoLongerLeaksIntoTheText)
{
    // 0.4.1 translated Qt::Key_R as "not ours" without Ctrl, so Shift+R fell
    // through to the widget and typed an 'r'.
    Fixture f(QStringLiteral("abc"));
    f.press(Qt::Key_R, Qt::ShiftModifier, QStringLiteral("R"));
    CHECK_EQ(f.filter->statusText(), QStringLiteral("-- REPLACE --"));
    CHECK(f.filter->replaceMode());
    CHECK(!f.spy.sawKeyPress); // the key never reached the widget as text
    f.type("XY");
    CHECK_EQ(f.text(), QStringLiteral("XYc"));
    f.press(Qt::Key_Escape);
    CHECK_EQ(f.filter->statusText(), QStringLiteral("-- NORMAL --"));
    CHECK_EQ(f.text(), QStringLiteral("XYc"));
}

TEST_CASE(ReplaceModeIsOneUndo)
{
    Fixture f(QStringLiteral("abc"));
    f.press(Qt::Key_R, Qt::ShiftModifier, QStringLiteral("R"));
    f.type("XY");
    f.press(Qt::Key_Escape);
    f.type("u");
    CHECK_EQ(f.text(), QStringLiteral("abc"));
}

// ---- Ctrl chords the core owns ----------------------------------------------

TEST_CASE(CtrlAIncrementsTheNumberUnderTheCursor)
{
    Fixture f(QStringLiteral("file1.txt"));
    f.press(Qt::Key_A, Qt::ControlModifier);
    CHECK_EQ(f.text(), QStringLiteral("file2.txt"));
    CHECK(!f.spy.sawKeyPress); // Ctrl+A is no longer "select all" here
    f.press(Qt::Key_X, Qt::ControlModifier);
    CHECK_EQ(f.text(), QStringLiteral("file1.txt"));
}

TEST_CASE(CtrlDHalfPagesAndIsConsumed)
{
    QStringList lines;
    for (int i = 0; i < 40; ++i)
        lines << QStringLiteral("line%1").arg(i);
    Fixture f(lines.join(QLatin1Char('\n')));
    f.press(Qt::Key_D, Qt::ControlModifier);
    CHECK(!f.spy.sawKeyPress);
    CHECK(f.caret().blockNumber() > 0); // moved down a half page (≥1 line)
    f.press(Qt::Key_U, Qt::ControlModifier);
    CHECK_EQ(f.caret().blockNumber(), 0);
}

TEST_CASE(CtrlSStillBelongsToTheWindow)
{
    Fixture f(QStringLiteral("abc"));
    f.press(Qt::Key_S, Qt::ControlModifier);
    CHECK(f.spy.sawKeyPress); // the window's "run the batch" chord passes through
    CHECK_EQ(f.text(), QStringLiteral("abc"));
}

TEST_CASE(HostCtrlZKeepsTheCoreInSync)
{
    // Ctrl+Z is the host's undo (it reaches the widget). The core has to notice,
    // or the next vim command edits stale text.
    Fixture f(QStringLiteral("abc"));
    f.type("x"); // core edit: "bc"
    CHECK_EQ(f.text(), QStringLiteral("bc"));
    f.edit.undo(); // the widget's own undo -> "abc"
    CHECK_EQ(f.text(), QStringLiteral("abc"));
    f.type("$"); // last char of the text the user now sees
    f.type("x");
    CHECK_EQ(f.text(), QStringLiteral("ab"));
}

// ---- selection rendering -----------------------------------------------------

TEST_CASE(VisualLineSelectionCoversWholeLines)
{
    Fixture f(QStringLiteral("abc\ndef\nghi"));
    f.type("V"); // visual line on line 0
    f.type("j"); // ... and line 1
    const QTextCursor caret = f.caret();
    CHECK(caret.hasSelection());
    CHECK_EQ(caret.selectionStart(), 0);
    // Both lines, newline included: "abc\ndef\n".
    CHECK_EQ(caret.selectedText().size(), 8);
}

TEST_CASE(VisualSelectionIncludesBothEnds)
{
    Fixture f(QStringLiteral("abcdef"));
    f.type("vlly"); // select "abc"
    CHECK_EQ(f.filter->statusText(), QStringLiteral("-- NORMAL --"));
    f.type("p"); // paste it after the cursor? cursor is at 0 -> after 'a'
    CHECK_EQ(f.text(), QStringLiteral("aabcbcdef"));
}

TEST_CASE(BackwardVisualSelectionKeepsTheAnchorChar)
{
    Fixture f(QStringLiteral("abcdef"));
    f.type("$");  // last char
    f.type("v");
    f.type("h");
    f.type("h");
    // Visual from 'f' back to 'd' must cover d,e,f.
    const QTextCursor caret = f.caret();
    CHECK(caret.hasSelection());
    CHECK_EQ(caret.selectedText(), QStringLiteral("def"));
}

// ---- caret shape -------------------------------------------------------------

TEST_CASE(BlockCaretInNormalAndLineCaretInInsert)
{
    Fixture f(QStringLiteral("abc"));
    CHECK(f.filter->blockCaretVisible());
    CHECK_EQ(f.edit.cursorWidth(), 0); // the native caret is hidden
    f.type("i");
    CHECK(!f.filter->blockCaretVisible());
    CHECK(f.edit.cursorWidth() > 0); // typing gets the ordinary caret back
    f.press(Qt::Key_Escape);
    CHECK(f.filter->blockCaretVisible());
}

TEST_CASE(BlockCaretDisappearsWhenVimIsOff)
{
    Fixture f(QStringLiteral("abc"));
    f.filter->setEnabled(false);
    CHECK(!f.filter->blockCaretVisible());
    CHECK(f.edit.cursorWidth() > 0);
}

int main(int argc, char** argv)
{
    // Offscreen: the test must not need a desktop session.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return vimqt_test::runAll();
}
