# ra9fael.rename UI Specification (Proposal)

**Status: specification; the UI logic is implemented and tested, the Qt rendering is not.** Bilingual links: [中文](UI-SPEC.md) · [README](README.en.md) · [Design](plugin-design.en.md) · [Test plan](TEST-PLAN.en.md) · [Implementation validation](docs/validation/rename-m0-20261110/IMPLEMENTATION.en.md). Wireframes use ASCII; control IDs are proposals pending verification with the real host.

## 1. Entry and main layout

The entry is a host action/menu for selected file results, `rename.open`, depending on the host `files` trigger contract (design RNF-07). Do not describe it as an existing Explorer context-menu integration. Explorer integration is a separate exploration phase.

The window top carries a **mode switch**: `[Control] [Text]`. Both modes share the same selection, the same finalization logic, and the same executor (RN-FR-20). If the other mode holds uncommitted edits, switching asks for confirmation first.

Stable IDs: `rename.mode-control`, `rename.mode-text`, `rename.input-summary`, `rename.rule-list`, `rename.rule-scope`, `rename.match-mode`, `rename.flags-panel`, `rename.preview-table`, `rename.preview-filter`, `rename.oil-editor`, `rename.oil-error-count`, `rename.execute`, `rename.cancel`, `rename.undo`.

## 2. Control mode

```text
+--------------------------------------------------------------------------+
| Rename   24 selected                    [Control] [Text]        [Reset]   |
+--------------------------------------------------------------------------+
| RULE 1  [Enabled] [Scope: Name v] [Literal v] [First match v] [Remove]   |
| Find: [draft            ]  Replace: [final             ]  [Add rule]      |
| Rule 2  [Enabled] [Scope: Extension v] [Regex v] [All matches v]         |
| Find: [^JPG$            ]  Replace: [jpg               ]                 |
+--------------------------------------------------------------------------+
| FLAGS  [x] RegEx  [ ] Case  [x] All matches  [x] Enumerate               |
|        Name only [ ]   Extension only [ ]   Randomize [ ]                |
|        Case: [Uppercase][Lowercase][Title Case][Capitalize]              |
|        Skip: [x] Files [x] Folders [ ] Subfolders                        |
+--------------------------------------------------------------------------+
| PREVIEW  24 items   [Selected v] [Filter matches...      ] [Sort: Name v] |
| [x] Original name         Match                 New name          Status  |
| [x] draft (1).txt         [draft]               final (1).txt     Ready   |
| [x] notes.txt             (no match)            notes.txt         Unchanged|
| [ ] draft?.txt            [draft]               Invalid name      Error   |
+--------------------------------------------------------------------------+
| 22 changes  1 unchanged  1 error      [Cancel] [Rename selected: 22]      |
+--------------------------------------------------------------------------+
```

### 2.1 Rule rows

Each rule carries an enabled checkbox, scope (Name / Extension / Full name), literal/regex, first/all, find, replace, and a remove button. Rule order is keyboard-adjustable through visible commands, never drag-and-drop only. The chain relationship is explicit: each rule's output feeds the next.

### 2.2 Flag panel (design §3.4)

Borrowing PowerRename's control grouping, trimmed for release one:

- **Matching**: `RegEx`, `Case sensitive`, `All matches`
- **Scope shortcuts**: `Name only`, `Extension only` (mutually exclusive with the rule-row scope; switching keeps both in sync)
- **Case transforms** (one of four, RN-FR-17): `Uppercase`, `Lowercase`, `Title Case`, `Capitalize`
- **Exclusions**: `Skip files`, `Skip folders`, `Skip subfolder contents`
- **Templates**: `Enumerate` (counter templates)
- **No `Randomize`** in release one (deliberately cut because it is not reproducible)

### 2.3 Preview table and filters (RN-FR-10)

Preview columns: selection, original name, highlighted match, target name, status/error.

Three orthogonal dimensions:

| Dimension | Values | Effect |
|---|---|---|
| Status | Ready / Unchanged / Error / TooLong / Exists | whether it can execute |
| Visibility | determined by the filter | what is shown |
| Selection | checkbox | whether it executes |

Clicking headers cycles filters: `All` → `Selected` → `Applicable` → `To rename` → `All`. Filtering changes visible rows only and **never the input set**.

The sort menu names the active basis; rule execution order and counter order are identified explicitly.

### 2.4 States and authorization

Empty state: `No files selected`, prompting the user to pick files from the host; never show fabricated files. When no rule matches, still show the original name with `Unchanged`.

Regex errors mark the rule and affected rows, and the stale plan dies immediately. Truncation notice: when the `files` array exceeds the length cap, show "only the first N items arrived; please select fewer" rather than truncating silently.

The execution confirmation lists the valid count, conflicts/errors, target summary, recursion state, collected roots, and file count. When a parent and its descendant both rename, the preview explicitly marks the parent/child relation and states the execution order (children first); it does not block. During execution, show per-item phase, current item, and success/failure; after cancellation, show completed and not-completed counts. On success, offer "Undo this batch"; undo re-verifies file identity and original-name availability and refuses on conflict without overwriting later files.

### 2.5 Finalization notices (design §3.5 / §3.6)

When trailing whitespace or dots are removed, the preview row shows a secondary `trailing whitespace/dot removed` notice rather than staying silent. When a name contains an NBSP, show `contains non-breaking space; treated as a plain space`.

## 3. Text mode (oil mode, RN-FR-18 / RN-FR-19)

```text
+--------------------------------------------------------------------------+
| Rename   24 selected                    [Control] [Text]        [Reset]   |
+--------------------------------------------------------------------------+
| Pre-check [x]   24 entries · 22 renames · 2 errors    [Apply: 22]  [Undo] |
+-----+--------------------------------------------------------------------+
|  1  | /1042 report-draft.txt                                             |
|  2  | /1043 report-final.txt                                             |
|  3  | /1044 photos/                                                      |
|  4  |       /1045 IMG_0001.jpg                                           |
|  5  | /1046 notes.txt                                                    |
|  6  | /1047 draft?.txt          <- invalid character                     |
|  7  | /1048 missing.txt         <- target already exists                 |
+-----+--------------------------------------------------------------------+
| 2 errors; cannot save.                                    [Cancel]       |
+--------------------------------------------------------------------------+
```

### 3.1 Editor

- Monospace font with a line-number gutter.
- The `/<id>` prefix renders in a secondary color and is **not editable** (the caret skips it); the leaf name is freely editable.
- Indentation is visual only and discarded during parsing (design RNF-36); **no folding or tree view** (RNF-35).
- Pasting a block from elsewhere leaves lines without `/<id>` prefixes, which all become creates → errors (RN-FR-19). The footer says: "text mode supports renames only in release one; use control mode for bulk generation."

### 3.2 Pre-check and error display

- 300 ms after typing stops, a background pre-check runs (can be disabled). It never writes to disk and never mutates the id table.
- Error rows get a light red background plus a squiggle under the leaf name, with an inline trailing note (`<- target already exists`).
- The footer always shows `N entries · M renames · K errors`.
- Any error disables `Apply`, whose label is fixed to `N errors; cannot save.`

### 3.3 Commit flow

`Ctrl+S` or `Apply` → blocking checks (error rows / target exists / buffer mixes renames with deletes) → confirmation dialog:

```text
+---------------------------------------------+
| Apply renames?                              |
|   22 rename   0 create   0 delete   0 error |
|   3 parent/child pairs will rename children |
|   first                                      |
|                        [Cancel]  [Rename 22] |
+---------------------------------------------+
```

Execution shows a progress window (following RNF-34): advancing item by item, cancellable, and after cancellation reports each item's location.

On completion the buffer resets to fresh `/<id>` lines, ids are reassigned, and the id table updates (otherwise a second edit would point at the old name).

### 3.4 Switching modes

If the current mode has uncommitted edits, switching asks: `Discard uncommitted text-mode edits?` Choosing to keep cancels the switch. The selection and recursion state are preserved.

## 4. Keyboard, accessibility, theme, scaling

### 4.1 Control-mode focus order

Input summary → mode switch → rule rows → rule fields → flag panel → preview filter/sort → result rows → execution actions. Tab/Shift+Tab navigate, Space toggles enabled/selected, arrow keys move table rows. Enter must not bypass the execution confirmation; Esc closes non-destructive dialogs.

### 4.2 Text-mode keys

`Ctrl+S` submits, `Ctrl+Z`/`Ctrl+Y` undo/redo edits, `Ctrl+A` selects all, arrows/Home/End move normally. `Home` jumps to the start of the leaf name (skipping the non-editable `/<id>`), `Ctrl+Home` goes to line start. Tab does not move focus between the gutter and the editor (the gutter is not focusable).

### 4.3 Accessibility

Match spans are conveyed by both highlight and textual status; errors carry an icon, text, and accessible description. Color never carries state alone; a screen reader must read each row's original name, target name, status, and error. Text-mode error rows require a color-independent readable description (the inline trailing note satisfies this).

Focus is visible and controls are labeled. Follow the host light/dark/high-contrast themes without hardcoding colors. Validate 100%-200% DPI, Qt font scaling, and long localized strings.

Long rule templates wrap or scroll horizontally without clipping key fields; in narrow windows the rule editor stacks vertically, the preview scrolls horizontally, and the bottom execute/cancel actions stay reachable. In text mode, narrow windows scroll horizontally rather than wrapping (wrapping would break the one-entry-per-line parsing intuition).

## 5. Unverified

No Qt UI, real host entry, screenshots, or keyboard/screen-reader validation exists. The host menu entry, window size constraints, scope/match controls, assistive-technology announcements, theme tokens, DPI/scaling, text-mode monospace metrics, and actual error-highlight rendering are all unverified. How `IFileOperation`'s elevation prompt actually appears inside the plugin process is also unverified.
