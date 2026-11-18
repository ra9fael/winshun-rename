# Changelog

## [0.5.0] - 2026-11-19

- New "Show Rename in file result menus" switch: `triggers.files.if` names the `showInFileMenu` bool setting (on by default), so the result-menu entry disappears when it is off while keywords keep working. Requires Win顺 0.33.0.
- A name typed after the keyword searches WinShun's index by file name: the first row "Batch rename the N matches…" opens the window on every match and the rows after it open one file each. This adds the `winshun:search` permission, which the plugin card asks you to confirm when you update.
- The right-click entry takes the selection as its input set: one file is a one-row batch, one folder renames that folder, and no parent-directory or folder-content scan happens; a folder's contents come from "Batch rename in this folder".
- Batches may span folders: batch duplicates and "the target is another row's current name" are decided per folder, so same-named files in different folders neither conflict nor wait for each other; each row shows its folder.
- Fixed: a selection with an unreadable path now yields no rows at all (enumeration used to keep what it had read before the failure); the window stays empty, shows the reason and cannot execute.
- New manifest gate `tools/check-manifest.ps1` (ctest `manifest_menu_switch`): `files.if` must name a bool setting that defaults to on, `minWinShun` must be at least 0.33.0, and using `search_files` requires declaring `winshun:search`.

## [0.4.5] - 2026-11-17

- Packaging: the `.wsplugin` carries README.md, README.en.md and LICENSE, as the plugin library requires; the packing script fails when one is missing.
- Added a root LICENSE (MIT), matching the `license` field in plugin.json.

## [0.4.4] - 2026-11-17

- Vim: `d`/`c`/`y` accept `f`/`F`/`t`/`T` motions (`df,` deletes through the comma, `dF,`/`dT,` go backwards, a motion that cannot move cancels the operator); `d;`/`d,` repeat the last find, and `;`/`,` repeat `t`/`T` correctly.
- Vim: `r` inside a `v`/`V`/Ctrl+Q selection replaces every selected character with one keystroke; block-wise it covers existing characters only (short rows are not padded); Esc cancels; the whole range is one undo step.
- Vim: `:q` closes the window; with unapplied edits it refuses (`E37: pending changes (use :q! to discard them)`), `:q!` discards them and closes.
- Input method: a Chinese IME no longer leaks text into the window in Normal/Visual and the other non-insert modes; Insert mode and vim-off behave as before.
- Fixed: with a find motion pending, `:` is now the character to search for (`t:`), as in vim, instead of opening the command line.

## [0.4.3] - 2026-11-16

- Status bar reworked: help on the far left, rename counts on the far right, the vim badge shows the mode letter (N/I/R/v/V/B); zero counts are neutral gray and "Rename" is never clipped.
- Text-mode DSL: each line is `state id name` with `y` rename, `n` skip, `d` delete (to the recycle bin); the id is the file's identity, names may contain spaces, deleting a whole line is an error.
- `:` find/replace previews live, with `:s` and `:%s`; `dd` deletions sync with the file list's right-click undo.
- Only checked rows participate in the rename; unchecked rows show "未选".
