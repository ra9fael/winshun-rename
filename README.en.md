# Batch Rename (ra9fael.rename)

A batch-rename plugin for Win顺: right-click selected files or folders to open the work window, preview the result built from a rule chain or a text buffer, then rename. The selection is the window's input set, so a selected folder is renamed itself; a folder's contents come from "Batch rename in this folder". For a single file, use Win顺's own F2.

**0.4.5 is released.** Source and releases live at [ra9fael/winshun-rename](https://github.com/ra9fael/winshun-rename); tag `v0.4.5` is commit `046f77b`. The release workflow builds that tag on windows-2022, runs the tests, packages, and attaches the `.wsplugin` to the release.
Language/document links: [中文](README.md) · [Design and requirements](plugin-design.en.md) · [UI specification](UI-SPEC.en.md) · [Test plan](TEST-PLAN.en.md) · [Vim mode](docs/VIM-MODE.md).

## Installing

- Plugin library: `Win顺 → Settings → Plugins → Plugin library`, search for "Batch Rename".
- Manually: download `ra9fael.rename-0.4.5.wsplugin` from the [v0.4.5 release](https://github.com/ra9fael/winshun-rename/releases/tag/v0.4.5) (a `.sha256` sits beside it) and install it.

Requires Win顺 0.33.0 or newer: the result-menu switch uses `triggers.files.if`, which hosts read from that version on. The package carries `Qt6Widgets.dll`: WinShun's shared-Qt contract does not include Qt Widgets yet, and the host supplies the rest of Qt 6.12.

## Entries

- Right-click: select files or folders and use "Batch rename…" in the result menu; the window's input set is exactly those items. The plugin card's "Show Rename in file result menus" (on by default) governs that entry only — keywords keep working with it off, and the plugin stays enabled.
- Current folder: while searching inside a folder, the first row "Batch rename in this folder…" opens the window on that folder's direct children.
- Keywords: `br`, `pl`, `rename` or 批量重命名 enter keyword mode. Typing a name after it searches WinShun's index by file name: the first row "Batch rename the N matches…" opens the window on every match and the rows after it open one file each; Enter on an empty keyword opens an empty window.
- Batches may span folders: duplicate-name detection compares within one folder only, so same-named files in different folders do not conflict; each row carries its folder, and the tooltip has the whole path.

## Permissions

| Permission | Why |
|---|---|
| `ui` | The plugin's own window: table, rule column, text buffer |
| `files:read` | List the selected folder's entries, read names and size/write-time attributes (to verify file identity before and after), and the image metadata that metadata templates use (jpg/jpeg/png/tif/tiff/heic/heif/avif only, screened by extension first) |
| `files:write` | Rename; deletes go through Shell `IFileOperation` to the Recycle Bin |
| `winshun:search` | Searching WinShun's file-name index when a name follows the keyword (`search_files`; names and paths only) |

No network, no launching programs outside the package, ordinary user rights. A batch changes file names, never file contents.

## Qt Widgets window

The window is Qt Widgets: `ui/widgets_window.*` holds the table, rule column and text editor, and `ui/qt_host.cpp` only creates the `QApplication` and the window. The core, `rename_ui::Presenter`, `Bridge` and the executor are unchanged. Build with `-DRENAME_BUILD_QT=ON` as before, but Qt Widgets is now required; `tools/pack.ps1` bundles `Qt6Widgets.dll` until WinShun's shared-Qt contract includes it. Measurements, screenshots and open items: [docs/validation/rename-widgets-20261115/README.md](docs/validation/rename-widgets-20261115/README.md).

## Vim mode

Text mode can take Vim keys; it is off by default. The core is the portable [`winshun-vimcore`](https://github.com/ra9fael/winshun-vimcore) library (pure C++20, no Qt, no Win32) in this repo as the `vendor/vimcore` submodule; the Qt adapter `ui/vim_qt.*` stays here, and the choice is stored in `<dataDir>/settings.json`.

Covered: `h j k l w b e 0 ^ $ gg G f F t T %`; `i a I A o O s R`; `d c y` with motions (including `f F t T` and the `;`/`,` repeats, `d;`/`d,` reusing the last find), text objects and doubled forms (`dd cc yy D C S x X Y p P J r ~`); `v`/`V`/Ctrl+Q selections including block-wise, with `r` replacing the selection with one character; count prefixes; `.` repeat; Ctrl+A/X; Ctrl+D/U/F/B paging; and the `:` command line (find/replace, `:s`, `:%s`, `:q`, `:q!`). Outside Insert mode an input method does not leak text into the buffer.

Key boundaries, Esc ownership, undo, the input method and the tests are in [docs/VIM-MODE.md](docs/VIM-MODE.md).

```bash
git clone --recurse-submodules <repo>      # or, after a plain clone:
git submodule update --init --recursive
```

Without the submodule CMake fails with that command rather than silently dropping Vim mode.

## Trying it locally (no host)

`rename-ui.exe` is a standalone window wrapper for verifying the interface and the logic without Win顺. It links the same code the plugin's window uses.

```
rename-ui.exe --dir <folder> [--recursive]      # the folder's direct children become the set
rename-ui.exe --files <path> [<path>...]        # these files
rename-ui.exe                                   # empty window
```

Qt is needed at run time: put `C:\Program Files\WinShun` on `PATH` and point `QT_PLUGIN_PATH`, `QT_QPA_PLATFORM_PLUGIN_PATH` and `QML_IMPORT_PATH` at the same folder — exactly what the host runner does.

Diagnostic hooks (verification only): `RENAME_GRAB_WINDOW=<png>` renders the window and exits; `RENAME_GRAB_RULE=<find>\n<replace>`, `RENAME_GRAB_MODE=text` and `RENAME_GRAB_APPLY=1` preset a rule, switch to text mode, or run the batch before the grab.

## Existing baseline

The factual baseline comes from `tmp/rename-comparison/day.rename/README.md`, `rename_core.py`, and `plugin.json`: day.rename already supports multi-rule Python regex, smart cleanup, date/file metadata, multiple numbering systems, manual multiline editing, optional AI naming, no-overwrite conflict handling, and two-phase rename with journal recovery. It is not a featureless starting point. Its current entry point is a plugin command, not a manifest `files` trigger; recursive processing is explicitly out of scope; the current behavior replaces all matches and has no extension-only mode.

## Two ways to provide input

The plugin offers **two modes** that share one finalization pipeline and one executor, so mode affects only how you type inputs, never correctness.

**Control mode** — describe "how to change" with a rule chain plus flags. Suited to structured renames: photo numbering, date templates, bulk prefixes/suffixes, extension normalization, case transforms.

**Text mode (oil mode)** — spread the file set into an editable text buffer, borrowing from [oil.nvim](https://github.com/stevearc/oil.nvim). One entry per line with a non-editable `/<id>` anchor prefix; `Ctrl+S` is the only thing that touches disk. Suited to scattered fixes, pasting results from a spreadsheet or script, and one-off visual review.

### Five ideas borrowed from oil.nvim

1. **The text is the data model** — the buffer is both input and preview, so there is no second "form plus preview table" state to keep in sync.
2. **The `/<id>` anchor is a prerequisite** — in plain text, changing `a.txt` to `b.txt` is byte-for-byte indistinguishable from deleting one line and adding another. The anchor is the only way to say "the same object, with a new name".
3. **Diffs, not commands, are the intermediate representation** — parsing emits only `new/delete/change`, and diff-to-action is a separate layer, so confirmation, progress, and undo all hang off the diff.
4. **Errors are structured, per-line, and non-aborting** — line + column + message, one bad line does not stop the rest, and errors highlight in the buffer.
5. **Writing is explicitly triggered and confirmed** — editing is not committing; a confirmation dialog and a progress window come first.

## Five ideas borrowed from PowerRename

Reference: the local PowerToys checkout at `PowerToys/src/modules/PowerRename/` (read-only; no source imported).

1. **The rule core is a pure function** — `Replace(source, flags, counter)` touches no disk, so it can re-run on every keystroke. Every behavior switch collapses into one bit flag.
2. **Preview runs on its own cancellable thread** — "typing mid-word" is always safe.
3. **Execution goes to Shell `IFileOperation`** — rename, undo, and elevation are all delegated, so no self-built recovery journal is needed.
4. **Deepest-first submission** — children rename before parents, then subtree paths are fixed, so parent/child renames no longer require rejecting the whole batch.
5. **Three finalization steps (trim → case transform → NFC equivalence)** — handles NFD filenames correctly and makes case-only renames safe.

## Deliberate deviations

- **No rename-on-collision.** PowerRename uses `FOF_RENAMEONCOLLISION` to turn conflicts into `name (1)`; we refuse instead and never overwrite.
- **We do not copy its dead code.** PowerRename defines `ItemNameAlreadyExists` but never assigns it; we actually implement target-exists detection.
- **Text mode does renames only.** oil.nvim supports create/delete/move/cross-directory; we narrow to renames (deletes go to the Recycle Bin) so the risk surface equals control mode.
- **No tree view.** Following oil.nvim's own judgment: a tree view is a different methodology.
- **No self-built recovery journal.** Rename and undo go to the Shell; after a crash we never auto-roll-back and keep only a read-only batch journal for diagnostics.

## Scope

Rule scopes: name, extension, full name; first or all matches; counter templates and date templates; metadata templates (EXIF/XMP, limited to 8 extensions); four case transforms; preview match highlighting, selection, filtering, and sorting; rule chains and presets.

Execution design explicitly handles cancellation, name swaps, parent/child directory combinations, case-only changes, Unicode and NFD, Windows reserved names, external changes during execution, and SMB behavior. Undo goes through the Shell undo stack and re-verifies file identity first, never overwriting a file created later. AI is not a core feature. Recursive collection is off by default; explicit opt-in collects files under selected roots without automatically renaming directories. Only the final file suffix is extension (`gz` for `archive.tar.gz`); `.gitignore` has none. Extension rules skip extensionless files with an explanation; directory extension is inapplicable. Trailing whitespace and dots are removed by the finalization rules, but preview reports it explicitly rather than silently.

## Source and build

This version's source is tag `v0.4.5` (commit `046f77b`). Build, test and package:

```powershell
git submodule update --init --recursive
cmake -S . -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DRENAME_BUILD_QT=ON `
  -DCMAKE_PREFIX_PATH=D:\tools\Qt\6.12.0\msvc2022_64
cmake --build build-qt --parallel
ctest --test-dir build-qt --output-on-failure
pwsh -File tools/pack.ps1 -BuildDir build-qt -OutDir dist
```

`ctest` is seven targets: `vim_qt`, `vim_window`, `rename_core`, `rename_e2e`, `core_purity`, `action_payload`, `manifest_menu_switch`. `vendor/vimcore` ships its own two suites, built separately (`-DVIMCORE_BUILD_TESTS=ON`).

Release is `.github/workflows/release.yml`: push a `v<version>` tag, and windows-2022 installs Qt 6.12, builds, runs the tests, packages with the same `pack.ps1`, and attaches the `.wsplugin` plus its paired `.sha256` to the release. `workflow_dispatch` only verifies, never publishes.

## Known issues

- Real mouse interaction (entering from the right-click menu, table checkboxes, the directory bar, the confirmation dialog) is verified with a host-like process and rendered grabs, not by clicking through each one.
- The input-method isolation outside Insert mode relies on unbinding the input method at the platform layer; Chinese IME composition on real hardware has not been exercised case by case.
- The ex command line stops at `:` (find/replace), `:s`, `:%s`, `:q`, `:q!`; named registers, macros, marks, `/`/`?` search and multiple buffers are out of scope.
- `Qt6Widgets.dll` ships inside the package for now; that item goes away once the shared-Qt contract covers Widgets.

## Documents

- [Design and 11-section requirements](plugin-design.en.md): facts (with per-item sources for both references), dual-mode architecture, requirement IDs, permissions, failure matrix, honest notes.
- [UI specification](UI-SPEC.en.md): one ASCII wireframe per mode, layout, fields, keyboard shortcuts, error states, theme/scaling, and accessibility.
- [Test plan](TEST-PLAN.en.md): row-level ID, FR, L0-L5, preconditions, steps, expected result, evidence, and unexecuted status, plus a "behavioural differences from the references" regression list.
- [Local verification spec](TEST-SPEC.en.md): what must be verified locally (S1–S9) before the host is involved, including the directory input set and the single-file path.
- [Vim mode](docs/VIM-MODE.md): trade-offs, key boundaries, the input method, and tests.
- [0.4.4 release record](docs/validation/rename-release-20261117/README.md): tag, CI run, artifact verification and local test results.
