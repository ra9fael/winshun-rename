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

    // Parses "pattern" or "pattern -> replace" (both also with the zh arrow
    // "→"). Everything around the arrow keeps its spaces, so "photo _ -> hol i
    // day _" means what it says.
    struct Pattern
    {
        std::wstring find;
        std::wstring replace;
    };
    static Pattern ParsePattern(const std::string& text)
    {
        const std::wstring wide = Widen(text);
        // The zh arrow first: "->" inside it would be misread.
        std::size_t arrow = wide.find(L"->");
        const std::size_t wideArrow = wide.find(L"\u2192");
        if (arrow == std::wstring::npos || (wideArrow != std::wstring::npos && wideArrow < arrow))
            arrow = wideArrow;
        if (arrow == std::wstring::npos)
            return {wide, std::wstring()};
        // The find half ends at the arrow's first half; trim spaces on both
        // halves (full-width too, a zh input's habit).
        const auto isSpace = [](wchar_t c) {
            return c == L' ' || c == L'\t' || c == L'\u3000';
        };
        std::size_t findEnd = arrow;
        while (findEnd > 0 && isSpace(wide[findEnd - 1]))
            --findEnd;
        std::size_t replaceStart = (wide[arrow] == L'\u2192') ? arrow + 1 : arrow + 2;
        const std::size_t end = wide.size();
        while (replaceStart < end && isSpace(wide[replaceStart]))
            ++replaceStart;
        std::size_t replaceEnd = end;
        while (replaceEnd > replaceStart && isSpace(wide[replaceEnd - 1]))
            --replaceEnd;
        return {wide.substr(0, findEnd), wide.substr(replaceStart, replaceEnd - replaceStart)};
    }

    // One file row, per the agreed design: title is the NEW name (the host
    // highlights the typed words inside it), subtitle is "← original" so the
    // direction reads right-to-left as "became, from". A blocked row explains
    // itself in red text via its subtitle and loses Enter. No per-row button:
    // Enter runs the batch, and a button on every row would suggest otherwise.
    winshun::Item PreviewRow(const rename_win::CollectedItem& item,
                             const rename_core::PlanItem& planned, bool zh)
    {
        const std::string original = Narrow(item.leaf);
        const std::string target = Narrow(planned.targetLeaf);
        const std::string status = rename_core::StatusId(planned.status).data();

        std::string subtitle;
        if (status == "ready")
            subtitle = "← " + original;
        else if (status == "unchanged")
            subtitle = zh ? "不变 · 没有替换词" : "unchanged · no replacement given";
        else if (status == "exists")
            subtitle = zh ? ("目标已存在：" + target + " · 这一项不会执行")
                          : ("target exists: " + target + " · this row will not run");
        else if (status == "too-long")
            subtitle = zh ? "名字过长，不会执行" : "name too long; will not run";
        else if (status == "invalid-character")
            subtitle = zh ? "含非法字符，不会执行" : "invalid character; will not run";
        else if (status == "empty-name")
            subtitle = zh ? "替换后名字为空，不会执行" : "name empty after replace; will not run";
        else
            subtitle = zh ? ("规则错误：" + target) : ("bad rule: " + target);

        // Title keeps the original when nothing changes: the name the user
        // recognises, with the host's own highlight on the typed words.
        winshun::Item row("rename.row." + item.id,
                          status == "ready" ? target : original, subtitle);
        row.withIcon(item.isFolder ? "glyph:U+E8B7" : "glyph:U+E8A5");
        row.withData(winshun::Json::Object {
            {"path", Narrow(item.fullPath)},
            {"leaf", original},
            {"target", target},
            {"folder", item.isFolder},
            {"status", status},
        });
        if (status != "ready" && status != "unchanged")
            row.invalid();
        return row;
    }

    winshun::Items query(const winshun::Query& q) override
    {
        if (q.trigger == "files")
            return {};

        const bool zh = chinese();
        const Pattern pattern = ParsePattern(q.text);

        // Bare "rename": two rows only. The search box is deliberately the
        // smallest thing that helps — rename one file, or open the window where
        // the real work happens. Batch rules and the oil mode live in the window;
        // growing them here would mean two UIs to keep in step.
        if (pattern.find.empty())
        {
            winshun::Items rows;

            const std::string t1 = zh ? "改名：rename 原名 -> 新名" : "Rename one file: rename old -> new";
            const std::string s1 = zh ? "只改一个文件时用；改名结果就在这一行，Enter 执行"
                                      : "For a single file; the result is right here, Enter runs it";
            winshun::Item one("rename.one", t1, s1);
            one.withIcon("glyph:U+E8AC");
            one.withCopy("rename ");
            rows.push_back(std::move(one));

            // The folder the host is browsing (a folder tab, or a folder filter)
            // is exactly "this directory": the window opens on its entries. With
            // no folder in view there is nothing to open *in*, so the row says
            // how to get there instead of opening an empty window.
            const std::string browsed = q.filters["folder"].isString()
                ? q.filters["folder"].toString() : std::string();
            if (!browsed.empty())
            {
                winshun::Item here("rename.here",
                                   zh ? "在此目录重命名…" : "Rename in this folder…",
                                   zh ? "打开重命名窗口，列出这个文件夹里的项目 · " + browsed
                                      : ("Open the rename window on this folder · " + browsed));
                here.withIcon("glyph:U+E8B7");
                here.withData(winshun::Json::Object {{"openFolder", browsed}});
                here.withScore(100);
                rows.push_back(std::move(here));
            }
            else
            {
                winshun::Item win("rename.window",
                                  zh ? "在文件夹里重命名…" : "Rename in a folder…",
                                  zh ? "在文件夹标签里输入 rename，或选中文件后右键“重命名…”"
                                     : "Type rename on a folder tab, or right-click selected files and pick Rename…");
                win.withIcon("glyph:U+E8B7");
                win.withData(winshun::Json::Object {{"openWindow", true}});
                rows.push_back(std::move(win));
            }
            return rows;
        }

        // "rename <find> -> <replace>": the one-file path. The search box is not
        // the batch tool, so this finds the file the words name and shows exactly
        // one row: the new name, with Enter as the only action.
        const winshun::Json found = tool("search_files", winshun::Json::Object {
            {"query", Narrow(pattern.find)},
            {"kind", "files"},
            {"limit", 5},
        });
        if (found.has("error"))
        {
            const std::string message = found["error"]["message"].isString()
                ? found["error"]["message"].toString() : std::string("search failed");
            winshun::Item row("rename.search-error", zh ? "搜索失败" : "Search failed", message);
            row.withIcon("glyph:U+E783").invalid();
            return {row};
        }

        std::vector<std::string> paths;
        const winshun::Json foundItems = found["items"].isArray() ? found["items"] : found["structuredContent"]["items"];
        if (foundItems.isArray())
        {
            for (const winshun::Json& entry : foundItems.toArray())
            {
                if (entry["path"].isString())
                    paths.push_back(entry["path"].toString());
            }
        }
        if (paths.empty())
        {
            winshun::Item row("rename.none", zh ? "没有匹配的文件" : "No matching files", Narrow(pattern.find));
            row.withIcon("glyph:U+E8AC").invalid();
            return {row};
        }

        // More than one match means the words were not specific enough for the
        // single-file path: say so and point at the window rather than renaming
        // whichever file the index happened to rank first.
        if (paths.size() > 1)
        {
            winshun::Item row("rename.ambiguous",
                              zh ? "匹配到 " + std::to_string(paths.size()) + " 个文件"
                                 : std::to_string(paths.size()) + " files match",
                              zh ? "搜索栏只改一个文件；批量请选中文件后右键“重命名…”，或用重命名窗口"
                                 : "The box renames one file; for batches select files and pick Rename…, or use the window");
            row.withIcon("glyph:U+E783").invalid();
            return {row};
        }

        fullPaths = paths;

        rename_win::EnumerateRequest request;
        request.recursive = false;
        for (const std::string& path : paths)
            request.roots.push_back(Widen(path));
        const rename_win::EnumerateResult collected = rename_win::Enumerate(request);
        if (collected.blocked || collected.items.empty())
        {
            winshun::Item row("rename.none", zh ? "读不到这个文件" : "Cannot read this file",
                              Narrow(collected.blockReason));
            row.withIcon("glyph:U+E783").invalid();
            return {row};
        }

        std::vector<rename_core::ItemSnapshot> items;
        items.reserve(collected.items.size());
        for (const rename_win::CollectedItem& entry : collected.items)
        {
            rename_core::ItemSnapshot snapshot;
            snapshot.id = entry.id;
            snapshot.leaf = entry.leaf;
            snapshot.isFolder = entry.isFolder;
            snapshot.depth = entry.depth;
            snapshot.pathLength = entry.pathLength;
            snapshot.templateContext = rename_win::BuildTemplateContext(entry, 0);
            items.push_back(std::move(snapshot));
        }

        // No arrow means no replacement, and an empty replacement would delete
        // the found text — never what "rename 报告" alone meant.
        if (pattern.replace.empty())
        {
            winshun::Item row("rename.need-replace",
                              zh ? "还需要新名字" : "A new name is needed",
                              zh ? "写成 rename " + Narrow(pattern.find) + " -> 新名字"
                                 : "Write it as rename " + Narrow(pattern.find) + " -> newname");
            row.withIcon("glyph:U+E8AC").invalid();
            return {row};
        }

        rename_ui::ControlState control;
        {
            rename_core::Rule rule;
            rule.enabled = true;
            rule.scope = rename_core::Scope::Name;
            rule.match = rename_core::MatchType::Literal;
            rule.occurrence = rename_core::Occurrence::All;
            rule.search = pattern.find;
            rule.replace = pattern.replace;
            control.chain.rules.push_back(std::move(rule));
        }

        rename_ui::Presenter presenter;
        presenter.SetControlState(std::move(control));
        presenter.SetItems(std::move(items));
        const rename_core::PreviewPlan& plan = presenter.Plan();

        const rename_core::PlanItem* one = plan.items.empty() ? nullptr : &plan.items.front();
        if (!one || one->status != rename_core::Status::Ready)
        {
            const std::string subtitle = !one ? std::string()
                : (zh ? "结果：" + Narrow(one->targetLeaf) + "（不会执行）"
                      : ("would become " + Narrow(one->targetLeaf) + " (will not run)"));
            winshun::Item row("rename.no-change",
                              zh ? "这一行不需要改" : "Nothing to change here", subtitle);
            row.withIcon("glyph:U+E8AC").invalid();
            return {row};
        }

        // One row, one action: the same row object the window's table uses.
        winshun::Item row = PreviewRow(collected.items.front(), *one, zh);
        winshun::Json::Array batch;
        batch.push_back(winshun::Json::Object {
            {"path", Narrow(collected.items.front().fullPath)},
            {"target", Narrow(one->targetLeaf)},
            {"folder", collected.items.front().isFolder},
        });
        row.withData(winshun::Json::Object {
            {"path", Narrow(collected.items.front().fullPath)},
            {"leaf", Narrow(collected.items.front().leaf)},
            {"target", Narrow(one->targetLeaf)},
            {"folder", collected.items.front().isFolder},
            {"status", rename_core::StatusId(one->status).data()},
            {"batch", std::move(batch)},
        });
        return {row};
    }

    winshun::Effects action(const winshun::ActionRequest& a) override
    {
        // What the row carried travels back in the item's `data` (the host hands
        // the item back as it came — Items.cpp::itemToJson — and `data` is where
        // a row keeps its own fields). Reading only the top level is how this
        // answered "No files selected" to a row that plainly had a batch.
        const winshun::Json payload = a.item["data"].isObject() ? a.item["data"] : a.item;

        // "Rename in this folder…": the window opens on that folder's entries.
        // With Qt this is a real window; without Qt there is nothing to draw, so
        // the answer says so instead of opening nothing.
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
            const std::string text = chinese()
                ? "重命名窗口：在文件夹标签里输入 rename，或选中文件后右键“重命名…”"
                : "Rename window: type rename on a folder tab, or right-click selected files and pick Rename…";
            return {winshun::effect::notify(text)};
        }

        // The batch row (and any preview row): rename what the row carries.
        if (a.trigger != "files" && payload["batch"].isArray())
        {
            std::vector<rename_win::ExecuteItem> batchItems;
            for (const winshun::Json& entry : payload["batch"].toArray())
            {
                rename_win::ExecuteItem item;
                item.fullPath = Widen(entry["path"].toString());
                item.targetLeaf = Widen(entry["target"].toString());
                item.isFolder = entry["folder"].toBool(false);
                item.depth = 0;
                batchItems.push_back(std::move(item));
            }
            if (batchItems.empty())
                return {winshun::effect::notify(chinese() ? "没有可执行的改名" : "Nothing to rename")};

            const rename_win::ExecuteResult result = rename_win::Execute(batchItems, m_dataDir);
            int renamed = 0, failed = 0;
            for (const rename_win::ExecuteResultItem& item : result.items)
            {
                if (item.outcome == rename_win::ExecuteOutcome::Renamed)
                    ++renamed;
                else
                    ++failed;
            }
            const std::string text = failed == 0
                ? (chinese() ? "已重命名 " + std::to_string(renamed) + " 项"
                             : "Renamed " + std::to_string(renamed) + " item(s)")
                : (chinese() ? "已重命名 " + std::to_string(renamed) + " 项，" + std::to_string(failed) + " 项未成功"
                             : "Renamed " + std::to_string(renamed) + ", " + std::to_string(failed) + " not done");
            return {winshun::effect::notify(text), winshun::effect::hide()};
        }

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
    std::vector<std::string> fullPaths;   // query 的行序，PreviewRow 按下标注入 data
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
