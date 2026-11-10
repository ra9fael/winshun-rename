// main.cpp — the resident plugin entry point.
//
// Serves the WinShun SDK protocol, keeps one Presenter as the model, and hands
// execution to rename_win.
//
// Threading. When this build has Qt (RENAME_BUILD_QT), the shape is the
// workspace's shared-Qt convention and matches ra9fael.envhosts and
// winshun.capture: Qt Quick needs the GUI thread, so the Qt event loop owns the
// main thread and the JSON-RPC loop moves to a worker thread that keeps stdio.
// Nothing but protocol messages is ever written to stdout.
//
// Without Qt the same protocol loop runs on the main thread and the plugin
// answers queries without a window. That is not a degraded build: it is what
// makes the whole pipeline testable on a machine with no Qt installed, and it is
// why every decision lives in core/ and presenter.cpp rather than here.
#include "ui/presenter.h"
#include "win/platform.h"
#include "winshun.h"

#include <cstdio>
#include <string>
#include <vector>

#ifdef RENAME_HAVE_QT
#include "ui/qt_host.h"

#include <QMetaObject>
#include <QString>

#include <thread>
#endif

namespace {

std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), needed);
    return out;
}

std::string Narrow(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), needed, nullptr, nullptr);
    return out;
}

class RenamePlugin : public winshun::Plugin
{
public:
    std::string name() const override { return "ra9fael.rename"; }

    // Set by main() before serve(): the appearance that arrived with initialize
    // has to reach the window, which is built on the GUI thread.
    void SetDataDir(const std::wstring& dataDir) { m_dataDir = dataDir; }

protected:
    void initialized() override
    {
        // The core compares names with NFC; without this it would compare raw
        // code units, which misreads a decomposed name as changed.
        rename_win::InstallNfcNormalizer();
        m_dataDir = Widen(dataDir());
#ifdef RENAME_HAVE_QT
        rename_host::SetDataDir(m_dataDir);
#endif
        PublishLook();
    }

    void appearanceChanged() override
    {
        // The host changed its look (theme or style): the open window takes the
        // new colours. Only sent to plugins with the `ui` permission, which this
        // one declares.
        PublishLook();
    }

    winshun::Items query(const winshun::Query& q) override
    {
        // The plugin's real entry point is the `files` trigger on a selection; a
        // keyword query just explains that, because offering rows here would
        // invent an input set the user never chose.
        if (q.trigger == "files")
            return {};

        const std::string title = chinese() ? "重命名选中的文件" : "Rename selected files";
        const std::string subtitle = chinese()
            ? "选中文件后在右键菜单里选择“重命名…”"
            : "Select files, then pick “Rename…” from the result menu";

        winshun::Item row("rename.hint", title, subtitle);
        row.withIcon("glyph:U+E8AC");
        row.withCopy(title);
        return {row};
    }

    winshun::Effects action(const winshun::ActionRequest& a) override
    {
        if (a.trigger != "files" || a.files.empty())
            return {winshun::effect::notify(chinese() ? "没有选中文件" : "No files selected")};

        // Probe P1 lives here: the stderr log is the only way to confirm that the
        // host's `files` trigger really arrived, how many paths came with it, and
        // how many characters the whole array used (the ~32000 cap, design
        // RNF-07). Plugin stderr goes to plugins.log, so this survives a crash.
        std::size_t characters = 0;
        for (const std::string& path : a.files)
            characters += path.size();
        std::fprintf(stderr, "[rename] files trigger: count=%zu chars=%zu\n",
                     a.files.size(), characters);
        for (const std::string& path : a.files)
            std::fprintf(stderr, "[rename]   %s\n", path.c_str());
        std::fflush(stderr);

        std::vector<std::wstring> roots;
        roots.reserve(a.files.size());
        for (const std::string& path : a.files)
            roots.push_back(Widen(path));

#ifdef RENAME_HAVE_QT
        // The window owns enumeration: it needs the rows anyway, and doing it
        // once on the GUI thread keeps one input set rather than two that could
        // disagree. Recursion stays off (RN-FR-14): there is no setting for it.
        rename_host::Show(roots, false);
        return {winshun::effect::hide()};
#else
        // No Qt in this build: report the honest state rather than inventing a
        // preview the user cannot see.
        rename_win::EnumerateRequest request;
        request.recursive = false;
        request.roots = roots;
        const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
        if (collected.blocked)
        {
            std::fprintf(stderr, "[rename] enumeration blocked: %s\n",
                         Narrow(collected.blockReason).c_str());
            return {winshun::effect::notify(Narrow(collected.blockReason))};
        }

        std::fprintf(stderr, "[rename] enumerated %zu item(s)\n", collected.items.size());
        const std::string message = chinese()
            ? ("已收到 " + std::to_string(collected.items.size()) +
               " 项；这个构建没有窗口")
            : ("Received " + std::to_string(collected.items.size()) +
               " item(s); this build has no window");
        return {winshun::effect::notify(message)};
#endif
    }

    void shuttingDown() override
    {
#ifdef RENAME_HAVE_QT
        rename_host::Quit();
#endif
    }

private:
#ifdef RENAME_HAVE_QT
    // The host's look, as `appearance()` says it now. `nativeLook` is passed as
    // compact JSON rather than a Qt type so this file stays Qt-free when the
    // window is not built.
    void PublishLook()
    {
        const winshun::Json& look = appearance()["nativeLook"];
        const std::string nativeLook = look.isObject() ? look.dump() : std::string();
        const std::string size = appearance()["uiSize"].isString()
            ? appearance()["uiSize"].toString()
            : std::string("standard");
        rename_host::ApplyLook(locale(), theme() == "dark", size, nativeLook);
    }
#else
    void PublishLook() {}
#endif

    std::wstring m_dataDir;
};

} // namespace

#ifdef RENAME_HAVE_QT

int main(int argc, char** argv)
{
    // Data dir is only known after initialize, which is on the protocol thread;
    // the window is created before that runs, so it takes the path later (see
    // RenamePlugin::initialized and Bridge::SetDataDir).
    std::string error;
    if (!rename_host::Start(argc, argv, std::wstring(), error))
    {
        // A scene failure must not leave a process with no window and a silent
        // protocol loop: say why and stop.
        std::fprintf(stderr, "[rename] window failed to load: %s\n", error.c_str());
        return 1;
    }

    RenamePlugin plugin;
    std::thread protocol([&plugin] {
        plugin.serve();
        // serve() returned: the host asked for shutdown or closed stdin. Quit the
        // GUI loop; it is the only thing keeping this process alive.
        rename_host::Quit();
    });

    const int rc = rename_host::Run();
    protocol.join();
    return rc;
}

#else

int main()
{
    RenamePlugin plugin;
    return plugin.serve();
}

#endif
