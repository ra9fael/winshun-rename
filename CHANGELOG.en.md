# Changelog

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
