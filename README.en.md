# Rename (Proposal)

**Status: implemented and tested (L1/L2), host not yet verified.** Design and requirements: [plugin-design.en.md](plugin-design.en.md). Implementation and validation records: `WinShun/docs/validation/rename-m0-20261110/` (core and executor) and `WinShun/docs/validation/rename-qt-20261110/` (Qt window and theme following). Language/document links: [中文](README.md) · [Design and requirements](plugin-design.en.md) · [UI specification](UI-SPEC.en.md) · [Test plan](TEST-PLAN.en.md).

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
- **Text mode does renames only in release one.** oil.nvim supports create/delete/move/cross-directory; we narrow to renames so the risk surface equals control mode.
- **No tree view.** Following oil.nvim's own judgment: a tree view is a different methodology.
- **No self-built recovery journal.** Rename and undo go to the Shell; after a crash we never auto-roll-back and keep only a read-only batch journal for diagnostics.

## Proposed capabilities

Rule scopes: name, extension, full name; first or all matches; counter templates and date templates; metadata templates (EXIF/XMP, limited to 8 extensions); four case transforms; preview match highlighting, selection, filtering, and sorting; rule chains and presets.

Execution design explicitly handles cancellation, name swaps, parent/child directory combinations, case-only changes, Unicode and NFD, Windows reserved names, external changes during execution, and SMB behavior. Undo goes through the Shell undo stack and re-verifies file identity first, never overwriting a file created later. AI is not a core feature in release one. Recursive collection is optional P1, off by default; explicit opt-in collects files under selected roots without automatically renaming directories. Only the final file suffix is extension (`gz` for `archive.tar.gz`); `.gitignore` has none. Extension rules skip extensionless files with an explanation; directory extension is inapplicable. Trailing whitespace and dots are removed by the finalization rules, but preview reports it explicitly rather than silently.

## Documents

- [Design and 11-section requirements](plugin-design.en.md): facts (with per-item sources for both references), dual-mode architecture, requirement IDs, permissions, failure matrix, honest notes.
- [UI specification](UI-SPEC.en.md): one ASCII wireframe per mode, layout, fields, keyboard shortcuts, error states, theme/scaling, and accessibility.
- [Test plan](TEST-PLAN.en.md): row-level ID, FR, L0-L5, preconditions, steps, expected result, evidence, and unexecuted status, plus a "behavioural differences from the references" regression list.

All features, compatibility, and performance are goals pending approval and validation. There is no implementation or test-pass claim at present.
