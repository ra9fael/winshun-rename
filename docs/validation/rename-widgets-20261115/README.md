# rename: Qt Widgets experiment

Date: 2026-11-15. Branch: `exp/qtwidgets`, worktree
`D:\dev\github\winshun-dev\ra9fael.rename\.worktrees\qtwidgets`.
Base: the working tree at `main` (`c1ebb99`) plus the uncommitted changes that
were present when the experiment started.

## What changed

- Replaced the Qt Quick window (`Main.qml` / `qt_host.cpp`) with
  `ui/widgets_window.{h,cpp}` and a Widgets host in `ui/qt_host.cpp`.
- The core, `rename_ui::Presenter`, `Bridge` and the executor are unchanged;
  `Bridge::previewModel()` now exposes three table columns over the same rows.
- Text mode is a `QPlainTextEdit` over `Bridge::buffer()`. The oil buffer is
  parsed by the existing `Presenter`; `Ctrl+S` and the button run the batch.
- The table has a real filter (`全部 / 已选 / 可改 / 要改`) and a select-all
  checkbox. `Bridge::setFilter()` was already implemented but had no caller.
- The apply button asks for confirmation, with the ready/unchanged/blocked
  counts. Mode switching asks before discarding unexecuted edits.
- `Bridge::setSelection()` / `setFolder()` now clear the previous done line,
  blocked line, filter, mode and oil buffer; a missing root is shown as an error
  instead of an empty set. `reset()` clears the same state.
- Apply results are matched back by the full planned target path, not by a leaf
  suffix, so equal leaf names in different folders cannot be mixed up.
- `package/plugin.json` raises `memoryMB` from 128 to 256 as headroom. The
  measured window is now ~10 MB private, so the old 128 MB cap was not the real
  cause; the Unicode-symbol/font fallback below was.
- The window no longer uses `✎`/`⏷`-style Unicode glyphs: U+270E alone loaded
  Segoe UI Emoji/Symbol and cost ~119 MB private on this machine. Arrows and
  checkmarks are painted now.
- The CJK-capable font (`Microsoft YaHei UI`) is selected before any child
  widget is created; selecting it later was too late because layout had already
  populated Qt's fallback font cache.
- `tools/pack.ps1` bundles `Qt6Widgets.dll` when the exe imports it. Qt Widgets
  is not in WinShun's shared-Qt contract yet.

## Build and tests

```powershell
cmake -S . -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DRENAME_BUILD_QT=ON -DCMAKE_PREFIX_PATH=D:\tools\Qt\6.12.0\msvc2022_64
cmake --build build-qt --parallel
ctest --test-dir build-qt --output-on-failure
```

Result: `ctest` 4/4 passed (`rename_core`, `rename_e2e`, `core_purity`,
`action_payload`). The Qt build compiles `rename.exe`, `rename-ui.exe` and the
same Widgets target used by the plugin.

## Screenshots

| Empty window (instructions, not an error) | Rule mode |
|---|---|
| ![empty](empty.png) | ![rule](rule.png) |

| Text mode | Filter: to rename |
|---|---|
| ![text](text.png) | ![filter](filter.png) |

| Folder contents | Missing path (blocked, not "no files") |
|---|---|
| ![folder](folder.png) | ![missing](missing.png) |

| Files trigger under the host Qt environment |
|---|
| ![trigger](host-files-trigger.png) |

## Files trigger

A host-like process (same environment as `tools/host-sim.ps1`) started
`rename.exe`, answered `initialize`, then sent:

```json
{"method":"action","params":{"command":"rename","action":"files",
 "trigger":"files","files":["…\\alpha.txt","…\\beta.txt"],"item":{},"text":""}}
```

It answered `{"effects":[{"type":"hide"}]}`, the plugin logged
`[rename] files trigger: count=1`, and the window rendered at 822×616. The
128 MB crash did not recur with the font/symbol fix and `memoryMB: 256`.

This is not the same as clicking the item in WinShun's GUI: the real host still
needs a package install with `Qt6Widgets.dll` beside the plugin exe (or Qt
Widgets added to the shared-Qt contract). The final experimental package is
`dist/ra9fael.rename-0.1.0.wsplugin` (3,295,311 bytes, SHA-256
`015a69fe12be048cef94f3a6187470514e5cafbca23c2791311765ac228e38ce`).

## Memory (this machine)

| Build | Working set | Private / commit | Notes |
|---|---:|---:|---|
| QML window, 128 MB cap | 324 MB peak (wrapper) | 164 MB commit at crash | `qBadAlloc`; also contained `✎`/`⏷` glyphs |
| Widgets, before glyph/font fix | 261 MB | 136 MB | same two rows |
| Widgets, final, two files | 50 MB | 12.5 MB | peak paged 27 MB |
| Widgets, final, Chinese filenames | 50 MB | 12.1 MB | peak paged 27 MB |
| Widgets under host Qt, files trigger | 45 MB | 8.6 MB | sampled with the window open |

### Why the first Widgets number was wrong

A minimal Qt Widgets probe isolated the cost; these are private/commit numbers
on this machine:

| Probe | Private |
|---|---:|
| English labels, default font | 10 MB |
| Chinese labels, default font | 122 MB |
| Chinese labels, `Microsoft YaHei UI` selected up front | 9 MB |
| The same plus a `✎` label (U+270E) | 119 MB |
| The same plus `⏷` (U+23F7) | 54 MB |

So the high number was **not Qt Widgets**. Qt's fallback from a Latin-only font
to a CJK face, and from normal text to Segoe UI Emoji/Symbol for `✎`/`⏷`, loads
large font caches. The QML window used the same glyphs and the same Latin-only
family, so its 164 MB crash was likely the same effect plus the QML engine; that
QML combination was not retested. Win32 GDI is only the answer if the target is
"single-digit MB"; Widgets is already around 10 MB private after the fix.

## Not verified yet

- A real WinShun right-click install/click with this package (the host-like
  process above is the closest so far).
- Light theme, 150/200 % DPI, screen reader, IME composition in the text editor.
- Clicking the table checkbox, the select-all box and the filter with a mouse;
  the filter screenshot used the `RENAME_GRAB_FILTER` diagnostic hook.
- The confirmation dialog's visual appearance under the host's four themes.
- The `.wsplugin` install/update path with the bundled `Qt6Widgets.dll`.

## Reproduce a screenshot

```powershell
$env:PATH = "D:\tools\Qt\6.12.0\msvc2022_64\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "D:\tools\Qt\6.12.0\msvc2022_64\plugins"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "D:\tools\Qt\6.12.0\msvc2022_64\plugins\platforms"
$env:RENAME_GRAB_WINDOW = "$PWD\build-qt\rule.png"
$env:RENAME_GRAB_RULE = "alpha`nomega"
.\rename-ui.exe --files C:\tmp\alpha.txt C:\tmp\beta.txt
```
