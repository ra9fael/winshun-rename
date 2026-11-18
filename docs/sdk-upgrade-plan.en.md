# Rename: result menu and folder navigation plan

[中文](sdk-upgrade-plan.md)

## Why change

Rename already declares a file/folder result-menu trigger and already moves rule chains, batch work and text editing into its independent work window. That structure fits the requested simple-search/complex-window split.

This round makes the existing menu entry configurable, verifies that it reaches the correct input set, and fixes navigation dropping the text tab. A separate preferences window is unnecessary while there are no substantial plugin preferences.

## User-facing scheme

- Keyword entries (`br`/`pl`/`rename`/批量重命名): Enter opens an empty window; "Batch rename in this folder…" — shown while searching inside a folder — opens that folder's direct children.
- Keep the existing Bulk Rename… action on selected file/folder results; the window's input set is exactly those items.
- Add one host-card boolean: Show Rename in file result menus, enabled by default.
- The rule/text tabs belong to the user: going up, refreshing and opening another folder all keep them, with the buffer rebuilt for the new folder.
- Keep rule chains, text mode, conflicts, preview and batch execution in the existing work window.

The boolean affects the result-menu entry only. It does not disable keyword commands or the plugin.

## Implementation

1. Keep the existing SDK header and protocol: `query`, `action` and the `files` parsing stay as they are; the plugin calls none of the host's tools.
2. Add a host-owned boolean key and reference it from the existing `triggers.files.if` field in `package/plugin.json`.
3. Retain the existing input maximum and file/folder kinds. A single selection is the input set too: one file is a one-row batch and one folder is renamed itself, with no parent-directory scan or folder-content listing; a folder's contents go through the "in this folder" entry.
4. Navigation keeps the mode: `Bridge::setFolder` notes whether the text mode is current, re-enumerates, replaces the input set and restores that mode, rebuilding the oil table from the new folder's items (the old buffer described the old set). A blocked enumeration keeps the tab too, with an empty list and the reason.
5. Preserve the existing preview and execution confirmation semantics. Querying must not rename or open the work window.
6. Older hosts ignore `if`, so `minWinShun` is `0.33.0`; `tools/check-manifest.ps1` turns the boolean item, its default, the `files.if` reference and that floor into a gate (ctest `manifest_menu_switch`), and the negative cases run against temporary manifests and have to fail.
7. A selection with an unreadable path is not loaded in part: enumeration returns an empty set, and the window shows the reason and refuses to execute. The window already dropped the rows of a blocked batch; this item adds the enumeration contract itself and the e2e assertion.
8. Batches may span folders: batch duplicates and "the target is another row's current name" are decided per folder (`core/plan.cpp`, `core/oil_differ.cpp`), so same-named files in different folders neither conflict nor wait for each other. When the batch spans folders the window shows each row's folder in the original column (the tooltip has the whole path); one-folder batches keep the leaf-only column.

No `settingsWindow` declaration is needed for a single host-owned switch. The independent rename work window remains a task interface rather than being relabeled as settings.

## Acceptance

- The menu entry is visible by default and disappears when its switch is off on a supporting host.
- Keywords still work while the menu entry is hidden.
- Single and multi-selection reach the work window with the exact selected paths.
- Going up, refreshing or opening another folder from the text tab leaves the user on the text tab, with the new folder's direct children in the buffer.
- A selection containing an unreadable path is not loaded in part: the window stays empty, shows the reason, and cannot execute.
- The current-folder search entry opens that folder according to its existing behavior.
- No file changes during querying or window opening; changes occur only through the existing execution flow.
- Relevant manifest, action-payload, navigation and rename integration tests pass using temporary files.

## Documentation

Explain the configurable result-menu entry, the difference between selected-input and current-folder workflows, tab-preserving navigation, and the host version needed for the visibility switch (`README.md`, `README.en.md`); record the version and behaviour changes in `CHANGELOG.md` / `CHANGELOG.en.md`; the verification record is `docs/validation/rename-menu-20261119/`.
