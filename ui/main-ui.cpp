// main-ui.cpp — the standalone window wrapper (local verification only).
//
// This is NOT the plugin. It exists so the window can be opened and driven on
// this machine without Win顺, without the plugin protocol, and without a
// .wsplugin: `rename-ui.exe --dir <folder>` or `rename-ui.exe --files a b c`.
//
// It links exactly the same UI code the plugin's window path uses
// (ui/bridge.cpp + ui/qt_host.cpp + rename_ui + rename_win), so what happens here
// is what will happen inside the host. The only thing it skips is the JSON-RPC
// loop in main.cpp.
//
// Usage:
//   rename-ui.exe --dir <folder> [--recursive]
//   rename-ui.exe --files <path> [<path>...] [--recursive]
//   rename-ui.exe                                  (empty window)
//
// Exit code 0 when the window ran and closed, 1 when the scene could not load.
#include "ui/qt_host.h"
#include "win/platform.h"

#include <QApplication>
#include <QByteArray>
#include <QTimer>

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    // Qt Quick needs the GUI thread; this wrapper has no protocol thread, so the
    // whole process stays on it.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Rename UI"));
    app.setQuitOnLastWindowClosed(true);

    const QStringList arguments = app.arguments();

    std::wstring folder;
    std::vector<std::wstring> files;
    bool recursive = false;
    bool wantFolder = false;

    for (int i = 1; i < arguments.size(); ++i)
    {
        const QString& argument = arguments[i];
        if (argument == QStringLiteral("--recursive"))
        {
            recursive = true;
        }
        else if (argument == QStringLiteral("--dir"))
        {
            wantFolder = true;
            if (i + 1 < arguments.size())
                folder = arguments[++i].toStdWString();
        }
        else if (argument == QStringLiteral("--files"))
        {
            wantFolder = false;
            // Everything that is not a flag belongs to this list, so a path with
            // spaces needs no special syntax: it is already one argv element.
            while (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--")))
                files.push_back(arguments[++i].toStdWString());
        }
        else if (!argument.startsWith(QStringLiteral("--")))
        {
            // A bare path: treat it as a file (the shortest useful invocation).
            files.push_back(argument.toStdWString());
        }
    }

    std::string error;
    if (!rename_host::Start(argc, argv, std::wstring(), error))
    {
        std::fprintf(stderr, "[rename-ui] window failed to start: %s\n", error.c_str());
        return 1;
    }

    // The wrapper's own look: one theme, deliberately. The plugin follows the
    // host's `appearance`; the wrapper exists to check the window and the logic,
    // so it pins a single palette (dark fluent) rather than pretending to cover
    // the host's styles. This round verifies that one theme only.
    const char* look =
        R"({"style":"fluent","dark":true,"rounded":true,"menuRadius":8,"cardRadius":6,)"
        R"("controlRadius":4,"largeControlRadius":6,"smallControlRadius":3,)"
        R"("background":"#ff202020","layer":"#ff272727","page":"#ff202020",)"
        R"("windowEdge":"#ff6a6a6a","divider":"#ff2e2e2e","text":"#fff3f3f3",)"
        R"("subtext":"#ffb8b8b8","faint":"#ff8a8a8a","hover":"#ff292929",)"
        R"("pressed":"#ff323232","tagFill":"#1effffff","control":"#ff373737",)"
        R"("controlHover":"#ff3c3c3c","controlBorder":"#ff444444",)"
        R"("inputFocused":"#ff1f1f1f","accent":"#ff4cc2ff","accentFill":"#ff0067c0",)"
        R"("accentText":"#ffffffff","danger":"#ffff99a4","dangerFill":"#ffc42b1c"})";
    const QByteArray locale = qgetenv("RENAME_UI_LOCALE");
    rename_host::ApplyLook(locale.isEmpty() ? "zh" : locale.constData(), true, "standard", look);

    if (wantFolder && !folder.empty())
        rename_host::ShowFolder(folder, recursive);
    else if (!files.empty())
        rename_host::Show(files, recursive);
    else
        rename_host::Show({}, recursive);   // empty window: the empty-state text

    return rename_host::Run();
}
