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
#include "ui/search_rows.h"
#include "win/platform.h"
#include "winshun.h"

#include <algorithm>
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

// The words after the keyword, without the spaces a user may have typed.
std::string Trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    const std::size_t last = text.find_last_not_of(' ');
    return text.substr(first, last - first + 1);
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

        // Words after `rename` are a name search: the keyword box is WinShun's
        // own search interface, so the plugin asks WinShun's index instead of
        // opening an empty window and leaving the user to browse to a folder.
        const std::string words = Trim(q.text);
        if (!words.empty())
            return SearchRows(words, browsed, q.limit, zh);

        winshun::Items rows;
        if (!browsed.empty())
        {
            winshun::Item here("batch.here",
                zh ? "在此文件夹批量重命名…" : "Batch rename in this folder…",
                zh ? "打开批量重命名窗口，列出这个文件夹里的项目 · " + browsed
                   : ("Open the batch rename window on this folder · " + browsed));
            here.withIcon("ph:textbox");
            here.withData(winshun::Json::Object {{"openFolder", browsed}});
            here.withScore(100);
            rows.push_back(std::move(here));
        }
        else
        {
            winshun::Item hint("batch.hint",
                zh ? "打开批量重命名…" : "Open Batch Rename…",
                zh ? "按 Enter 打开窗口；窗口里可以选择文件夹。在 rename 后面打名字可以直接搜索"
                   : "Press Enter to open the window and choose a folder there; type a name after rename to search");
            hint.withIcon("ph:textbox");
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

        // A row from the keyword list: the whole match set, or one file. Both
        // become the window's input set exactly as a right-click selection does.
        std::vector<std::wstring> chosen;
        for (const winshun::Json& path : payload["openPaths"].toArray())
            if (!path.toString().empty())
                chosen.push_back(Widen(path.toString()));
        if (payload["openPath"].isString() && !payload["openPath"].toString().empty())
            chosen.push_back(Widen(payload["openPath"].toString()));
        if (!chosen.empty())
            return ShowSelection(chosen);

        if (a.trigger != "files" || a.files.empty())
            return {winshun::effect::notify(chinese() ? "没有选中文件或文件夹" : "No file or folder selected")};

        std::vector<std::wstring> paths;
        paths.reserve(a.files.size());
        for (const std::string& path : a.files)
            paths.push_back(Widen(path));

#ifdef RENAME_HAVE_QT
        // The selection is the input set: one file, one folder and a mixed
        // group mean the same thing, so the window shows exactly these paths
        // instead of scanning a parent folder or listing a folder's contents.
        // A folder's contents come from the "Batch rename in this folder" row
        // shown while searching inside it, which routes to ShowFolder.
        //
        // Paths from more than one folder are fine: the plan compares targets
        // per folder (core/plan.cpp), so same-named files in different folders
        // neither conflict nor wait for each other. That is what makes a
        // search-driven batch possible at all.
        return ShowSelection(paths);
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
    // Rows for `rename <words>`: WinShun's own index answers, the first row
    // opens the batch on every match, then the first few matches on their own.
    // `folder` is the scope the launcher is searching in (WinShun's
    // `filters.folder`), so the search stays inside it when there is one.
    // Runs on the protocol thread, the only thread Plugin::tool() may be used
    // from (it reads its answer off the same stream serve() does).
    winshun::Items SearchRows(const std::string& words, const std::string& folder, int limit, bool zh)
    {
        winshun::Json::Array scope;
        if (!folder.empty())
            scope.push_back(folder);
        winshun::Json arguments = winshun::Json::Object {
            {"query", words},
            {"kind", "files"},   // a folder is reached from its own row, not a search hit
            {"size", false},     // names only: sizes would go to disk on every keystroke
            {"limit", std::min(limit, 200)},
        };
        if (!scope.empty())
            arguments.set("folders", winshun::Json(std::move(scope)));
        const winshun::Json answer = tool("search_files", std::move(arguments));
        if (answer["error"].isObject())
        {
            std::fprintf(stderr, "[rename] search failed: %s\n",
                         answer["error"]["message"].toString().c_str());
            std::fflush(stderr);
            winshun::Item failed("batch.unavailable",
                zh ? "搜索不可用" : "Search is unavailable",
                zh ? "Win顺 没有给出索引；用文件夹或结果菜单打开窗口"
                   : "Win顺 gave no index; open the window from a folder or the result menu");
            failed.withIcon("ph:textbox");
            return {std::move(failed)};
        }

        std::vector<rename_ui::SearchHit> hits;
        for (const winshun::Json& item : answer["items"].toArray())
        {
            const std::string path = item["path"].toString();
            if (path.empty())
                continue;
            hits.push_back({path, item["name"].toString()});
        }

        if (hits.empty())
        {
            winshun::Item none("batch.none",
                zh ? "没有匹配「" + words + "」的文件" : "No file matches \"" + words + "\"",
                zh ? "换个词再搜；回车打开空窗口" : "Try other words; Enter opens an empty window");
            none.withIcon("ph:textbox");
            none.withData(winshun::Json::Object {{"openWindow", true}});
            return {std::move(none)};
        }

        winshun::Items rows;
        for (const rename_ui::SearchRow& row : rename_ui::BuildSearchRows(words, hits,
                 answer["truncated"].toBool(false), zh))
        {
            const bool all = row.kind == rename_ui::SearchRow::Kind::All;
            winshun::Item item(all ? "batch.all" : "batch.one", row.title, row.subtitle);
            item.withIcon(all ? "ph:textbox" : "ph:file-text");
            winshun::Json data = winshun::Json::Object {};
            if (all)
            {
                winshun::Json::Array paths;
                for (const std::string& path : row.paths)
                    paths.push_back(path);
                data.set("openPaths", winshun::Json(std::move(paths)));
            }
            else
            {
                data.set("openPath", row.paths.front());
            }
            item.withData(std::move(data));
            item.withScore(all ? 100 : 90);
            rows.push_back(std::move(item));
        }
        return rows;
    }

    // Every entry ends here: the paths the host named become the window's input
    // set. The log lists each path, which is the record that the selection
    // arrived whole (tools/host-sim.ps1 reads it back).
    winshun::Effects ShowSelection(const std::vector<std::wstring>& paths)
    {
        std::fprintf(stderr, "[rename] window: count=%zu\n", paths.size());
        for (const std::wstring& path : paths)
            std::fprintf(stderr, "[rename]   %s\n", Narrow(path).c_str());
        std::fflush(stderr);
#ifdef RENAME_HAVE_QT
        rename_host::Show(paths, false);
        return {winshun::effect::hide()};
#else
        return {winshun::effect::notify(chinese()
            ? "批量重命名需要带 Qt 的构建"
            : "Batch Rename needs the Qt build")};
#endif
    }

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
