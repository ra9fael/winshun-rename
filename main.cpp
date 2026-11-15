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
#include "win/platform.h"
#include "winshun.h"

#include <cstdio>
#include <cwchar>
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

bool IsDirectory(const std::wstring& path)
{
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring ParentOf(std::wstring path)
{
    while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash == 0)
        return {};
    if (slash == 2 && path.size() >= 3 && path[1] == L':')
        return path.substr(0, 3); // "C:\"
    return path.substr(0, slash);
}

bool SameParent(const std::vector<std::wstring>& paths)
{
    std::wstring parent;
    for (const std::wstring& path : paths)
    {
        const std::wstring current = ParentOf(path);
        if (current.empty())
            return false;
        if (parent.empty())
            parent = current;
        else if (::_wcsicmp(parent.c_str(), current.c_str()) != 0)
            return false;
    }
    return !parent.empty();
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
        // The batch window is the only entry. Single-file rename is WinShun's
        // own F2; this plugin never renames a name typed into the box.
        if (q.trigger == "files")
            return {};

        const bool zh = chinese();
        const std::string browsed = q.filters["folder"].isString()
            ? q.filters["folder"].toString() : std::string();

        winshun::Items rows;
        if (!browsed.empty())
        {
            winshun::Item here("batch.here",
                zh ? "在此文件夹批量重命名…" : "Batch rename in this folder…",
                zh ? "打开批量重命名窗口，列出这个文件夹里的项目 · " + browsed
                   : ("Open the batch rename window on this folder · " + browsed));
            here.withIcon("glyph:U+E3B4");
            here.withData(winshun::Json::Object {{"openFolder", browsed}});
            here.withScore(100);
            rows.push_back(std::move(here));
        }
        else
        {
            winshun::Item hint("batch.hint",
                zh ? "打开批量重命名…" : "Open Batch Rename…",
                zh ? "按 Enter 打开窗口；窗口里可以选择文件夹"
                   : "Press Enter to open the window; choose a folder there");
            hint.withIcon("glyph:U+E3B4");
            hint.withData(winshun::Json::Object {{"openWindow", true}});
            rows.push_back(std::move(hint));
        }
        return rows;
    }
    winshun::Effects action(const winshun::ActionRequest& a) override
    {
        const winshun::Json payload = a.item["data"].isObject() ? a.item["data"] : a.item;

        if (payload["openFolder"].isString())
        {
            const std::string folder = payload["openFolder"].toString();
#ifdef RENAME_HAVE_QT
            rename_host::ShowFolder(Widen(folder), false);
            return {winshun::effect::hide()};
#else
            return {winshun::effect::notify(
                chinese() ? "这个构建没有窗口：" + folder : "This build has no window: " + folder)};
#endif
        }

        if (payload["openWindow"].toBool(false))
        {
#ifdef RENAME_HAVE_QT
            rename_host::Show({}, false); // empty window with the directory bar
            return {winshun::effect::hide()};
#else
            return {winshun::effect::notify(chinese()
                ? "批量重命名需要带 Qt 的构建"
                : "Batch Rename needs the Qt build")};
#endif
        }

        if (a.trigger != "files" || a.files.empty())
            return {winshun::effect::notify(chinese() ? "没有选中文件或文件夹" : "No file or folder selected")};

        std::size_t characters = 0;
        for (const std::string& path : a.files)
            characters += path.size();
        std::fprintf(stderr, "[rename] files trigger: count=%zu chars=%zu\n",
                     a.files.size(), characters);
        for (const std::string& path : a.files)
            std::fprintf(stderr, "[rename]   %s\n", path.c_str());
        std::fflush(stderr);

        std::vector<std::wstring> paths;
        paths.reserve(a.files.size());
        for (const std::string& path : a.files)
            paths.push_back(Widen(path));

#ifdef RENAME_HAVE_QT
        if (paths.size() == 1)
        {
            // One folder: its contents. One file: the files beside it.
            if (IsDirectory(paths.front()))
            {
                rename_host::ShowFolder(paths.front(), false);
            }
            else
            {
                const std::wstring parent = ParentOf(paths.front());
                if (parent.empty())
                    return {winshun::effect::notify(chinese() ? "找不到所在文件夹" : "The parent folder is unknown")};
                rename_host::ShowFolder(parent, false);
            }
        }
        else
        {
            // The current host builds the menu from the manifest alone and has
            // no per-path filter, so the entry is visible for any multi-select.
            // Refuse a cross-folder selection here rather than guessing which
            // folder the user meant.
            if (!SameParent(paths))
                return {winshun::effect::notify(chinese()
                    ? "请选择同一目录下的文件或文件夹"
                    : "Select files or folders in one directory")};
            rename_host::Show(paths, false); // exactly the selected items
        }
        return {winshun::effect::hide()};
#else
        return {winshun::effect::notify(chinese()
            ? "批量重命名需要带 Qt 的构建"
            : "Batch Rename needs the Qt build")};
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
