# ra9fael.rename Plugin Design Proposal

**Status: requirements document; the core logic is implemented and tested, host integration is not.** Date: 2026-11-10. Follows the workspace `DEVELOPMENT.md` 11-section design skeleton. Chinese: [plugin-design.md](plugin-design.md) · [README](README.en.md) · [UI specification](UI-SPEC.en.md) · [Test plan](TEST-PLAN.en.md) · [Implementation validation](../WinShun/docs/validation/rename-m0-20261110/IMPLEMENTATION.en.md).

## 1. Verdict

Ship a **dual-mode** bulk-rename plugin on a native C++/Qt shared runtime: **control mode** describes "how to change" via a rule chain plus flags, while **oil mode** presents the file set as an editable text buffer (borrowing oil.nvim's `/<id>` anchor technique). Both modes emit the same immutable preview plan and hand it to the same Shell executor (`IFileOperation`, deepest-first submission), so **mode affects only how you type inputs, never correctness**.

Entry uses only the host's supported `files` trigger and never claims existing Explorer context-menu integration. Every target must be planned, conflict-analyzed, and user-confirmed before any disk change. Release one's oil mode **permits renames only** and rejects create/delete/move/link changes, which puts both modes at the same risk surface. AI is not core in release one; Explorer integration is listed for exploration only.

## 2. Grounded facts

### 2.1 Existing baseline (day.rename)

| ID | Fact | Source and evidence level |
|---|---|---|
| RNF-01 | day.rename's README describes local find/replace, smart cleanup, insertion, auto-numbering, manual editing, and optional AI, with preview-before-execute and undo. | `tmp/rename-comparison/day.rename/README.md` §1-§5; read directly |
| RNF-02 | `rename_core.py` uses Python `re`; replacement rules run in order and call `re.sub`, so each rule currently replaces every occurrence. | `rename_core.py` L116-L126; source read |
| RNF-03 | day.rename supports NFKC, whitespace/separator cleanup, leading-number stripping, case transforms, creation/modification date metadata, Arabic/Chinese/Latin numbering and templates, and full-name manual entry. | `README.md` §4-§5, `rename_core.py` L57-L113, L127-L162; read directly |
| RNF-04 | day.rename executes in two phases through random temporary names, refuses to overwrite targets, journals file identity and paths, and attempts recovery on failure after verifying the original name is free. It explicitly is not a filesystem-wide atomic transaction. | `README.md` §Execution and recovery; `rename_core.py` L184-L213, L216-L321; read directly |
| RNF-05 | day.rename's manifest is currently a resident Python plugin with keywords `pl`/`cmm` and no `files` trigger declaration. | `plugin.json` L1-L42; read directly |
| RNF-06 | day.rename notes that adding a folder does not recursively change its contents and has no extension-only scope. Its README says the final extension is preserved by default, extension editing is optional, and folders are treated by full name. | `README.md` §File selection and extensions; read directly |
| RNF-07 | The host `files` trigger carries only a path array: both `trigger` and `action` are `files`, `WS_FILES` is a JSON array of paths, the whole array is capped near 32000 characters (the host then asks the user to select fewer), and there is **no item concept**, so selection state must be maintained by the plugin. | `WinShun/docs/plugins.md` "triggers" and "protocol, once"; read directly |
| RNF-08 | The host offers own-window resident plugins the `ui` permission, an `appearance` message, and a shared Qt contract (`runtime.qt: "host"`, guaranteeing Qt Core/Gui/Qml/Quick plus `QtQuick.Controls.Basic`). | `WinShun/docs/plugins.md` "resident plugins with their own window" and "shared Qt contract for native plugins"; read directly |

### 2.2 Borrowed reference one: PowerToys PowerRename

Local checkout: `PowerToys/src/modules/PowerRename/` (read-only reference; not modified, no source imported).

| ID | Fact | Source |
|---|---|---|
| RNF-09 | The rule core is a pure replacement function `Replace(source, &result, enumIndex)`: it takes a string and bit flags and returns a new name plus an advanced counter index, touching no disk. | `lib/PowerRenameInterfaces.h` L9-30, `lib/PowerRenameRegEx.cpp` L435-634 |
| RNF-10 | Every behavior switch collapses into one `DWORD`: case sensitivity, match-all, regex, enumeration, exclude files/folders/subfolders, name-only/extension-only, four case transforms, randomize, three timestamps, two metadata sources; persisted wholesale in settings. | `PowerRenameInterfaces.h` L9-30, `lib/Settings.h` |
| RNF-11 | Item status is a five-value enum `Init / ShouldRename / ItemNameTooLong / ItemNameInvalidChar / ItemNameAlreadyExists`; `ShouldRenameItem` is a pure predicate (selected AND renamable AND actually changed AND not excluded AND status allows). **Note that `ItemNameAlreadyExists` is never assigned anywhere — it is dead code.** | `PowerRenameInterfaces.h` L41-47, `lib/PowerRenameItem.cpp`; no assignment found in `Renaming.cpp` |
| RNF-12 | Visibility is orthogonal to status: `IsItemVisible(filter, flags)` with filters `None / Selected / FlagsApplicable / ShouldRename`, switched by clicking headers; filtering changes visible rows only, never the set. | Same, plus `lib/PowerRenameManager.cpp` `SwitchFilter` |
| RNF-13 | Preview runs on a dedicated worker thread: any input change (search term, replace term, flags, time, metadata) triggers `_PerformRegExRename()`, cancels the old thread, starts a new one, and reports back via `PostMessage` while the main thread pumps messages. | `PowerRenameManager.cpp` L439-467, L930-990 |
| RNF-14 | Execution does not hand-roll a rename loop; the whole batch goes to Shell `IFileOperation` with `FOF_ALLOWUNDO \| FOFX_ADDUNDORECORD \| FOFX_SHOWELEVATIONPROMPT \| FOF_RENAMEONCOLLISION`, and undo comes from the Shell undo stack. | `PowerRenameManager.cpp` L16, L848-860 |
| RNF-15 | Submission is deepest-first: compute maximum depth, build a depth matrix, add items from the deepest layer down to layer 0 so children are renamed before parents, then fix subtree paths via `UpdateChildrenPath` after a parent moves. | `PowerRenameManager.cpp` L757-800, `UpdateChildrenPath` |
| RNF-16 | The counter index is shared across the whole batch and advances **only when a match actually occurred** (`shouldIncrementCounter`); unmatched items consume no number. | `PowerRenameRegEx.cpp` L583-605 |
| RNF-17 | Name split/join is explicitly two-staged: `NameOnly` sends the stem to replacement and re-joins the original extension; `ExtensionOnly` sends the dot-stripped extension and re-joins the stem; extensionless plus `ExtensionOnly` returns the original name. | `lib/Renaming.cpp` L107-115, L219-235 |
| RNF-18 | Results must pass three finalization steps: `GetTrimmedFileName` (strip leading whitespace, trailing whitespace and dots) → `GetTransformedFileName` (case transforms) → `IsSameName` (equivalence check). | `Renaming.cpp` L245-266 |
| RNF-19 | Equivalence is checked by Unicode NFC normalization followed by character comparison, deliberately not `lstrcmp` (so `ß` differs from `ss`); inputs pass through `SanitizeAndNormalize` (NBSP→space, NFC). | `Renaming.cpp` L12-33, `PowerRenameRegEx.cpp` L23-55 |
| RNF-20 | Invalid characters are a hardcoded set `< > : " \ / \| ? *`; length uses a delta against 260 (files) / 247 (directories): `original path length + (new name length - original name length)`. | `Renaming.cpp` L286-299 |
| RNF-21 | Date templates: `$YYYY $YY $Y $MMMM $MMM $MM $M $DDDD $DDD $DD $D $HH $H $hh $h $mm $m $ss $s $fff $ff $f $TT $tt`; month/weekday names come from `GetDateFormatEx` in the user locale; `$$` escapes; longer patterns run before shorter ones, and negative lookahead stops `$D` from matching `$DATE_TAKEN_*` and `$H` from matching `$HEIGHT`. | `lib/Helpers.cpp` L393-505 |
| RNF-22 | Enumeration templates `${start=N,increment=N,padding=N}` are captured by regex with recorded byte offsets in the replacement string and inserted at those offsets; padding is reduced modulo `MAX_PATH`, falling back to zero padding if the buffer cannot fit. | `lib/Enumerating.h`, `lib/Enumerating.cpp` |
| RNF-23 | Metadata (EXIF/XMP) applies only to `jpg/jpeg/png/tif/tiff/heic/heif/avif`, screened by extension before template lookup, extracted through WIC with a cache; when a value is unavailable the `$PATTERN` literal stays in the name rather than silently becoming empty. | `Helpers.cpp` L322-390, L513-592 |
| RNF-24 | Regex supports both `std::wregex` and `boost::wregex`, each compiled in ECMAScript mode and selected by a setting; "first match only" uses `format_first_only`. | `PowerRenameRegEx.cpp` L421-434 |
| RNF-25 | The regex core has a fuzz entry point (three-way split of input/find/replace) plus 22 manager cases and 40 helper cases. | `PowerRename.FuzzingTest/`, `unittests/` |

### 2.3 Borrowed reference two: oil.nvim

Read online from `github.com/stevearc/oil.nvim` (read-only reference; no source imported).

| ID | Fact | Source |
|---|---|---|
| RNF-26 | Core claim: edit a directory like an ordinary buffer — one entry per line, edit the text freely, and only `:w` actually touches disk. | README "Quick start" |
| RNF-27 | **Every line starts with a `/<id> ` anchor prefix** (e.g. `/123 filename.txt`). Parsing uses the id to recognize "which entry in this batch this is"; same id with a new name means rename, a vanished id means delete, and **a line without an id is always treated as new**. This is the only mechanism that lets plain text distinguish a rename from a delete-then-create. | `lua/oil/mutator/parser.lua` `parse_line`, `M.parse` |
| RNF-28 | Directory lines end with `/` or `\`; symlinks are written `name -> target`. | `parser.lua` `parsedir`, `parse_line` |
| RNF-29 | Parsing produces only three diff kinds: `new` (with id = rename, without id = create), `delete` (original line vanished), `change` (column value changed); a rename is expressed as a same-id `new`. | `parser.lua` `M.parse`, `mutator/init.lua` |
| RNF-30 | Diff-to-action mapping is a separate layer: same-id `new` → `move` (src→dest); id-less `new` → `create`; `delete` attaches to the same-id diff as `has_delete` → `delete`; `change` → `change`. | `mutator/init.lua` `create_actions_from_diffs` |
| RNF-31 | Duplicate-name checks normalize by filesystem semantics: on Windows/macOS names are lowercased before comparison, and duplicates report `Duplicate filename`. | `parser.lua` `check_dupe` |
| RNF-32 | Parse errors are a structured table of line, column, and message — never thrown; one bad line does not stop the others. Error kinds include malformed ID, missing corresponding entry (id was changed), name containing a path separator, path starting with `/`, and duplicate name. | `parser.lua` `M.parse` `errors` |
| RNF-33 | A confirmation dialog precedes writing; `skip_confirm_for_simple_edits` can skip confirmation for simple operations, and `prompt_save_on_select_new_entry` urges saving before opening a new entry. | README options table |
| RNF-34 | Execution shows a dedicated progress window that advances action by action (`progress:set_action(action, idx, #actions)`) and can be cancelled mid-flight. | `mutator/init.lua` L386-490 |
| RNF-35 | Tree view is deliberately unsupported. The README states plainly that a tree view needs a wholly different methodology and amounts to a rewrite. | README "FAQ" |
| RNF-36 | Indentation is discarded entirely during parsing (`vim.trim`) and serves only as visual hierarchy; cross-directory editing relies on nested paths and brace alternation in new lines. | `parser.lua`, `mutator/init.lua` |

## 3. Architecture

### 3.1 Target shape (pending approval)

A native C++/Qt shared runtime (`runtime.qt: "host"`) wired into the host's real plugin template. The file set arrives through the host `files` trigger (RNF-07): paths only, no items, so both selection state and the oil-mode id table live inside the plugin. The host contract, `files` schema, permissions, and minimum version still need verification against the real host before implementation; if the host lacks that entry point, do not fall back to claiming Explorer integration.

The pure rule core has no Qt or OS headers. Given filenames/metadata/rule snapshots (or an oil buffer plus id table), it returns a change plan, match ranges, warnings, and conflicts. C++/Qt UI only edits rules or text, presents preview, and gathers confirmation. The Windows execution adapter handles identity checks and submission; it **does not hand-roll two-phase renames** — renames and undo go to Shell `IFileOperation` (RNF-14). UI code never changes files directly.

### 3.2 Dual-mode structure

Both modes share one rule core and one executor; only the source of "old name → new name" differs.

```text
                     +----------------------------------+
                     |  rename_core  (pure C++, no OS)   |
                     |  RuleEngine / TemplateExpander    |
                     |  NameSplitter / NfcCompare        |
                     |  OilParser / OilDiffer            |
                     |  StatusEvaluator / PlanBuilder    |
                     +----------------------------------+
                                  ^
                                  | emits an immutable PreviewPlan
                     +------------+-------------+
                     |                          |
        [ControlMode]                [OilMode]
         rule chain + flags           parse text buffer
         -> Evaluate()                -> Diff -> Plan
                     |                          |
                     +------------+-------------+
                                  |
                                  v
                     +----------------------------------+
                     |  rename_win  ShellExecutor        |
                     |  IFileOperation, deepest-first     |
                     +----------------------------------+
```

The key point: **oil mode bypasses the rule engine**. Its "old name → new name" mapping is written directly by the user, so no `Replace()` is needed. But it **reuses the same finalization logic** (trim / case transform / NFC equivalence / invalid characters / length / status enum) and **the same executor**. That means the correctness argument only needs to be made once.

Mode switching happens inside one window and preserves the selected file set; switching discards the other mode's uncommitted edits and therefore requires confirmation.

### 3.3 Stable requirement IDs and priorities

| ID | Priority | Requirement | Acceptance summary |
|---|---|---|---|
| RN-FR-01 | P0 | Start only from the host `files` trigger; clearly show the input set | Entry does not claim Explorer support; selection is plugin-maintained and matches the host array; exceeding the ~32000-character cap prompts the user to select fewer |
| RN-FR-02 | P0 | Rule scopes name/extension/full, re-split at every step | Each scope's input range and directory policy are explicit in preview and tests; trailing whitespace/dots are handled by finalization and surfaced explicitly |
| RN-FR-03 | P0 | First/all matching per rule, ordered rule chain | First/all differences are stable; each chain step consumes the preceding output; counter scoping is explicit |
| RN-FR-04 | P0 | Preview: debounce + worker thread + `planId` invalidation | Preview equals core output; stale plans die immediately; only selected valid items can execute |
| RN-FR-05 | P0 | Conflict/invalid-name/path/reserved-name checks | Stable errors shown; no existing item is ever overwritten; **target-exists detection is actually implemented** |
| RN-FR-06 | P0 | Execution via `IFileOperation`; undo via the Shell undo stack | No rename-on-collision flag; cancel/failure never overwrites later files; the batch journal is auditable |
| RN-FR-07 | P0 | Swaps, case-only changes, Unicode, parent/child selection, SMB boundaries | Supported scope validated on Windows/SMB; unsafe cases explicitly rejected |
| RN-FR-08 | P1 | Date and counter templates | Template parsing is deterministic pure logic; the pattern list includes negative lookahead; invalid fields block execution |
| RN-FR-09 | P1 | Rule-chain presets | A preset is a rule chain plus flags; contains no file paths or private data; versioned format |
| RN-FR-10 | P1 | Orthogonal status / visibility / selection filtering and sorting | Filtering does not change the input set; clicking headers switches filters |
| RN-FR-11 | P2 | Two regex engines (std / boost, both ECMAScript) | Engine is configurable; only explicitly declared subsets are claimed; compatibility is marked only after real core tests pass |
| RN-FR-12 | P2 | AI naming candidate | Not core in release one; no network or data-export permission without approval |
| RN-FR-13 | P3 | Explorer context-menu integration exploration | Separate host/Windows design and validation; no claims before exploration completes |
| RN-FR-14 | P1 | Explicit recursive file collection, off by default | Off means zero traversal; on shows roots, counts and provenance without automatically renaming directories; toggling invalidates preview |
| RN-FR-15 | P0 | Parent and descendant renamed together: deepest-first submission | Children rename first, parents after, then subtree paths are fixed; no wholesale rejection, but the parent/child relation is surfaced in preview |
| RN-FR-16 | P1 | Metadata templates (EXIF/XMP) | Limited to 8 extensions; screened by extension first; unavailable values keep the `$PATTERN` literal |
| RN-FR-17 | P1 | Four case transforms | Uppercase / Lowercase / Title Case (with exception word list) / Capitalize; applies to directories too |
| RN-FR-18 | P0 | **Oil mode**: text-buffer editing + anchor parsing + diff-to-rename | Buffer is editable and pasteable; parsing yields structured per-line errors; output is a preview plan isomorphic to control mode |
| RN-FR-19 | P0 | **Oil mode safety boundary**: renames only in release one | Create/delete/move/link changes are all flagged as error lines and refused; risk surface equals control mode |
| RN-FR-20 | P0 | **Both modes share the executor and finalization** | Both modes yield the same status for the same final name; mode switching preserves selection and requires confirmation before discarding uncommitted edits |

A directory may be a single explicitly host-selected rename item. Only RN-FR-14 enables traversal of user-selected roots to collect files. Do not follow symlinks/junctions or leave roots by default; deduplicate overlapping roots by file identity and report read failures instead of silently executing an incomplete set. Recursion never automatically selects directories for rename.

### 3.4 Flag layout

Borrowing the RNF-10 layout, trimmed for release one:

```text
0x0001 case sensitive      0x0002 match all        0x0004 regex
0x0008 counters enabled    0x0010 exclude files    0x0020 exclude folders
0x0040 exclude subfolders  0x0080 name only        0x0100 extension only
0x0200 uppercase           0x0400 lowercase        0x0800 title case
0x1000 capitalize          0x2000 randomize (cut in release one)
0x4000 created time / 0x8000 modified time / 0x10000 accessed time
0x20000 EXIF / 0x40000 XMP
```

`0x2000` randomize is cut because it is not reproducible and directly conflicts with "preview equals the final result".

### 3.5 Rule and compatibility model (control mode)

Fixed RN-FR-02 semantics: re-split the preceding rule's output at each step. `extension` is only the nonempty suffix after the final dot, excluding the separator, with a name before that dot. For `archive.tar.gz`, name=`archive.tar` and extension=`gz`; `.gitignore` and `README` have no extension. `name` changes only the stem and preserves the final suffix and separator; `full` changes the complete leaf name, never its path. For extensionless files, `name` equals `full`; `extension` skips by default without creating a suffix, even when a regex matches the empty string, and displays "No extension; extension rule not applied". For directories, `name`/`full` both mean the complete directory leaf name; `extension` is not applicable, leaves it unchanged and explains why.

**Trailing whitespace and dots (the RNF-18 ruling)**: follow PowerRename's trim — strip leading whitespace, trailing whitespace and dots — but **preview must explicitly report "trailing whitespace/dots removed"** rather than staying silent. This does not contradict "a trailing dot is an invalid name": the **result** is what must be legal, and trimming is **the means to produce a legal result**.

A rule node carries stable `ruleId`, enabled flag, scope, match type (literal/regex), search text, replacement template, case policy, and occurrence policy.

### 3.6 Finalization, three steps (shared by both modes)

Order is fixed and must not be reordered:

```text
1. Trim         strip leading whitespace; strip trailing whitespace and dots
2. Transform    apply case transforms per flags (Uppercase / Lowercase / Title Case / Capitalize)
3. Compare      NFC-normalize and compare against the original leaf; equal means "unchanged"
```

`Compare` uses NFC rather than `lstrcmp` (RNF-19). This handles NFD filenames correctly and makes case-only renames safe. `Transform` only reports an error if the result is still empty or invalid.

### 3.7 Templates and counters

Date patterns are borrowed verbatim from RNF-21: `$YYYY $YY $Y`, `$MMMM $MMM $MM $M`, `$DDDD $DDD $DD $D`, `$HH $H` (12-hour), `$hh $h` (24-hour), `$TT $tt`, `$mm $m`, `$ss $s`, `$fff $ff $f`. Collision avoidance is borrowed too: longer patterns first; `$D` uses negative lookahead excluding `DATE_TAKEN_`/`ESCRIPTION`/`OCUMENT_ID`; `$H` excludes `EIGHT`. `$$` escapes.

Counter templates `${start=N,increment=N,padding=N}` (RNF-22). **Counter scoping is the only substantive difference from PowerRename:**

- the index is shared across the whole batch (continuing across items), not reset per rule;
- multiple counter templates in one chain share that single index (two `${}` in one item print the same number);
- the index advances only if "that chain matched at least once on that item" (following RNF-16's intuition).

### 3.8 Oil mode specification (essence of RNF-26…RNF-36, narrowed to renames)

#### Buffer format

```text
/1042 report-draft.txt
/1043 report-final.txt
/1044 photos/
      /1045 IMG_0001.jpg
/1046 notes.txt
```

- Each line is `/<id><space><text>`; `id` is assigned by the plugin when the buffer is populated, and **users must not change it**.
- A trailing `/` or `\` marks a directory (RNF-28).
- Indentation is discarded during parsing and serves only as visual hierarchy (RNF-36); **no tree view** (RNF-35).
- Release one does not support the ` -> ` link syntax (the optional part of RNF-28); its presence is an error line.

#### Anchor semantics (RNF-27, the crux of oil mode)

| Buffer change | Interpretation | Release-one action |
|---|---|---|
| Same id, different leaf name | rename | ✅ `RenameItem` |
| A line with an id vanished | delete | ❌ refused: `release one does not support delete` |
| A new line with no `/<id>` | create | ❌ refused: `release one does not support create` |
| Line order changed | no diff | ✅ allowed (order does not mean reordering) |
| The `/<id>` itself was edited | no matching entry | ❌ refused: `that entry no longer exists (was the id changed?)` |
| Leaf name contains `/` or `\` | would move the item | ❌ refused: `a rename cannot move an item` |

**Why an anchor is required**: in plain text, changing `report-draft.txt` to `report-final.txt` is byte-for-byte indistinguishable from "delete one line and add another". The `/<id>` anchor is the only way to express "the same object, with a new name". Therefore the id table is the single source of truth and the buffer is merely its editable projection.

#### Parsing and diffing (RNF-29 / RNF-30)

```text
buffer text + id table
      ↓ OilParser (line by line, errors do not abort)
per-line record { id, leaf, isDir, rawLine, error? }
      ↓ OilDiffer
OilDiff[]: Rename{id, newLeaf} | Rejected{line, reason}
      ↓ reuse finalization + status evaluation
PreviewPlan (isomorphic to control mode)
```

The diff-to-action layer borrows RNF-30's separation: release one keeps only the `Rename` action, while `Create`/`Delete`/`Move` become `Rejected` at the diff layer and never reach the action layer. That leaves the action layer a single code path and shrinks the test surface sharply.

#### Error model (RNF-32)

Structured, per-line, non-aborting:

| Error | Line | Handling |
|---|---|---|
| Malformed `/<id> ` prefix | ✓ | Mark that line; parse the rest normally |
| No matching id | ✓ | `that entry no longer exists (was the id changed?)` |
| Empty leaf name | ✓ | Refuse |
| Name contains a path separator | ✓ | Refuse (a rename cannot move) |
| Duplicate within the batch (normalized by FS semantics) | ✓ | Refuse and mark both lines |
| Invalid characters `< > : " \ / \| ? *` | ✓ | Refuse |
| Length exceeded (260/247 delta) | ✓ | Refuse |
| Target already exists | ✓ | Refuse (this is `ItemNameAlreadyExists`) |
| New line without an id | ✓ | `release one does not support create` |
| Line vanished | ✓ | `release one does not support delete` |
| ` -> ` link syntax | ✓ | `release one does not support link changes` |

All errors highlight inside the buffer, and the footer shows "N errors; cannot save". Any error blocks submission.

#### Commit pipeline

```text
user edits the buffer
   ↓ (300 ms after typing stops, only when pre-check is enabled)
background pre-check: parse → diff → finalization → status → inline marks / footer counts
   ↓
user presses Ctrl+S or clicks Apply
   ↓
blocking checks (any one refuses without writing): error lines / target exists / buffer mixes renames with deletes
   ↓
confirmation dialog: N renames · 0 creates · 0 deletes · M errors
   ↓
executor: IFileOperation, deepest-first, cancellable progress window (RNF-34)
   ↓
on completion: buffer resets to fresh /<id> lines, ids reassigned, id table updated
```

#### Differences from control mode

| Dimension | Control mode | Oil mode |
|---|---|---|
| Input | rule chain + flags | edit target name text directly |
| Uses `Replace()` | yes | no |
| Batch power | strong (templates, counters, metadata, case transforms) | weak (whatever the user writes or pastes) |
| Works with external editors | no | whole blocks can be copied and pasted |
| Suited to | structured renames (photo numbering, dates, bulk prefixes) | scattered fixes, pasting results from a spreadsheet or script |
| Preview form | separate table | the buffer itself is the preview |
| Shared | finalization, status enum, conflict checks, executor, progress window | same |
| Release-one action surface | rename | rename only |

## 4. Core logic

### 4.1 Pure core API

```cpp
// Control mode: one replacement. Pure function, no disk access, errors via out-params.
struct ReplaceResult {
    std::wstring text;
    std::vector<MatchSpan> hits;   // match ranges for highlighting
    bool matched = false;          // decides whether the counter advances
    RuleError error = RuleError::None;  // BadRegex / BadTemplate / ...
};
ReplaceResult Replace(std::wstring_view source,
                      const Rule& rule,
                      const TemplateContext& ctx,
                      int* counter);

// Control mode: rule chain
ItemOutcome Evaluate(const ItemSnapshot& item,
                     const RuleChain& chain,
                     Flags flags,
                     int* counter);

// Oil mode: parse buffer -> diffs (per-line errors, never throws)
struct OilParseResult {
    std::vector<OilDiff> diffs;    // Rename{id,newLeaf} | Rejected{line,reason}
    std::vector<OilError> errors;  // line, col, message
};
OilParseResult ParseOilBuffer(std::wstring_view buffer,
                              const std::vector<OilEntry>& idTable);

// Shared by both modes: finalization, fixed order
std::wstring Finalize(const std::wstring& candidate,
                      std::wstring_view originalLeaf,
                      Flags flags,
                      bool isFolder,
                      Status* status);
```

### 4.2 Preview pipeline (RNF-13)

```text
any input change
   ↓ (50 ms debounce)
cancel the current preview thread (signal cancel, wait for exit)
   ↓
new thread: Evaluate() per item, or apply diffs -> PreviewPlan
   ↓ notify ITEM_READY(idx) per item (incremental table refresh)
   ↓ notify PLAN_COMPLETE when done
main thread: the finished plan replaces the old one; the old planId dies immediately
```

Execution accepts only the current `planId`. Any rule, buffer, or input change invalidates it.

### 4.3 Execution pipeline (RNF-14 + RNF-15)

```text
1. Freeze the PreviewPlan (validate planId)
2. Re-verify file identity per item (size/time/volume serial); mismatch fails that item
3. Group by depth (depth = level relative to the selected root)
4. From the deepest layer down to layer 0, call IFileOperation::RenameItem for items that should rename
5. SetOperationFlags(ALLOWUNDO | ADDUNDORECORD | SHOWELEVATIONPROMPT)
   Note: no RENAMEONCOLLISION — conflicts were already refused in step 2
6. PerformOperations()
7. After a parent directory renames, fix subtree paths (UpdateChildrenPath semantics)
8. On success offer "undo this batch" via the Shell undo stack; on failure report each item's current location
```

`JournalLite` records only "batch id, original path, new path, result per item" for diagnostics and reporting; **it does not provide automatic recovery**, never attempts rollback after a crash, and only points the user at the Shell undo stack. This is the explicit boundary against the self-built recovery journal described for day.rename in RNF-04.

Cancellation takes effect at safe boundaries: report completed, in-flight, and not-started items; never promise instant termination or an atomic batch rollback.

## 5. Interaction

Stable command/control IDs, ASCII wireframes, and keyboard behavior are in [UI-SPEC.en.md](UI-SPEC.en.md). The entry point defaults to the host-provided selection without scanning directories. Only explicit RN-FR-14 opt-in expands it with files under selected roots, showing provenance and count.

**Control mode**: the rule editor explicitly selects scope, first/all, regex/literal, and chain order; the flag panel offers four case transforms, name-only/extension-only, and exclude files/folders/subfolders; the preview table shows original name, match spans, target name, and status with selection, filtering, and sorting; clicking headers switches filters.

**Oil mode**: a monospace text editor with non-editable ids on the left, error lines marked red with squiggles, and live counts in the footer; `Ctrl+S` submits and opens a confirmation; execution shows a cancellable progress window.

The execution confirmation shows the number of renames, unmatched items, conflicts, and the target summary. Batch undo is offered on completion, with state re-checked first. AI remains a future candidate only; release one performs no AI network action by default.

## 6. Permissions, data, privacy

Textual manifest draft (proposal, not a generated manifest): `runtime.mode: resident`, `runtime.sandbox: normal`, `runtime.qt: "host"`; permissions `ui` (own window), `files:read` (host selection, rule metadata, explicit recursive roots), `files:write` (renames and the batch journal). See the [host permissions section](../WinShun/docs/plugins.en.md#permissions) and the shared Qt contract: normal file access is declarative, not a path sandbox, so exact path declarations and the minimum version still require implementation validation. When a target needs higher privileges, `IFileOperation` raises the elevation prompt itself (`FOFX_SHOWELEVATIONPROMPT`); the plugin neither silently drops privileges nor elevates on its own.

The plugin traverses selected roots only after explicit RN-FR-14 opt-in, never reads file contents, and reads only the metadata fields its rules need (limited to the 8 extensions in RNF-23). The batch journal lives in the host-provided data directory with restricted access and contains no file contents. Diagnostics avoid full personal paths unless the user explicitly exports and confirms. Oil mode's id table and buffer exist **only in memory and the data directory** and contain no file contents.

Release one does not make AI core and requests no `network` permission. Any future AI proposal must separately define the names/metadata sent, service, redaction, consent, cancellation, and key storage, and must pass permission approval. Presets store rules and flags only, never file lists.

## 7. Failure matrix

| Condition | Detection/risk | Expected handling |
|---|---|---|
| Empty selection / missing host context | Input set unknown | Show an actionable error; do not scan disk |
| `files` array exceeds the length cap | Only part of the paths arrive | Tell the user to select fewer; never truncate silently |
| Regex syntax error / catastrophic slow match | Invalid rule or UI stall | Mark immediately; bound and allow cancelling core execution; never run a stale preview |
| Windows invalid characters / reserved device name / length overflow | Invalid target path | Validate by Windows rules, block the target, name the offending field |
| Target conflict, duplicate within batch, external race | Overwrite risk | Refuse (no `RENAMEONCOLLISION`); re-check before execution |
| Name swaps / case-only edits | Intermediate target collision | Handled by `IFileOperation`; report an explicit error rather than staying silent if unsupported |
| Parent and descendant renamed together | Paths move with the parent | Deepest-first: children before parents, then fix subtree paths; surface the parent/child relation in preview |
| Unicode normalization / combining characters | Comparison diverges from disk semantics | Compare after NFC normalization (RNF-19); never rewrite bytes on disk silently |
| Name contains NBSP | NFC normalization turns it into a plain space | Surface "contains non-breaking space, treated as a plain space" during finalization; never silent |
| Entire string is whitespace/dots | Index underflow risk | Judge it an invalid name and error; the implementation must avoid unsigned index underflow |
| User or another process modifies/replaces a file mid-execution | Stale preview / wrong object | Stop that item or the batch on identity change; never modify the replacement |
| User cancellation | Partially executed batch | Apply at safe boundaries; report each item's location; never claim rollback |
| Process crash | Partially executed batch | No automatic rollback; point to the Shell undo stack and keep the batch journal |
| `IFileOperation` elevation denied | Insufficient privileges | Show the Shell's reason and per-item status; no silent retry |
| SMB disconnect | Submission interrupted | Report exactly which items were and were not renamed; never replay automatically |
| Time/metadata unreadable | Non-deterministic date template result | Keep the `$PATTERN` literal and mark that item's error; never substitute the current time silently |
| **oil: `/<id>` was edited** | No matching entry | Mark that line as an error, parse the rest normally, and forbid submission |
| **oil: buffer mixes renames with deletes/creates** | Outside the release-one boundary | Flag the delete/create lines as errors and refuse to write; never downgrade to renaming only part |
| **oil: whole block pasted from elsewhere** | No `/<id>` prefixes | Every line becomes new → `release one does not support create`; suggest using control mode for bulk generation |
| Future AI service unavailable / conflicting output | External failure or unsafe names | Release one does not depend on AI; future output stays a preview subject to local validation and confirmation |

## 8. Delivery checklist

This task delivers only eight bilingual documents; it creates no manifest, source, Qt/CMake configuration, dependency list, test script, or Explorer registration. After approval, add the verified host entry/manifest, third-party dependencies and licenses, preset and batch-journal formats, version changes, the package, and test evidence.

## 9. Test plan (L0-L5)

Per-case rows and traceability are in [TEST-PLAN.en.md](TEST-PLAN.en.md). L0 manifest/package static checks; L1 pure core; L2 fake filesystem/host dependency injection; L3 real WinShun host round-trip (completion gate); L4 Windows/SMB interactive E2E; L5 calibration and release. L1/L2 have run (85 unit cases green); the e2e part of L3 has run (14 real-rename assertions green); **the host round-trip itself has not run**, and L4/L5 are all unexecuted. Quantified targets are budgets, not measurements. The measured record is in the [implementation validation](../WinShun/docs/validation/rename-m0-20261110/IMPLEMENTATION.en.md).

## 10. Honest notes

**Measured:** Read day.rename's README, core, and manifest; read the host `plugins.md` sections on the `files` trigger, the protocol, and the shared Qt contract; read the core of the local PowerToys checkout at `src/modules/PowerRename/` (rule function, flags, status enum, preview thread, `IFileOperation` submission, deepest-first ordering, finalization steps, NFC equivalence, template engine, enumerator offsets, metadata extraction, fuzz and unit tests); read oil.nvim's README, parser, and mutator. Line numbers are in §2.

**Inferred:** A native C++/Qt shared runtime could remove the separate Python-environment dependency; a host `files` entry could pass the selected set to the rename tool; `IFileOperation`'s deepest-first ordering is sufficient to handle parent/child renames safely. These are architecture goals, not facts about host support.

**Deliberate deviations (not missing capability):**
- **No `FOF_RENAMEONCOLLISION`.** PowerRename uses it to auto-rename conflicts to `name (1)`; our RN-FR-05 demands never overwriting, so a conflict is refused.
- **No `ItemNameAlreadyExists` dead-code status.** PowerRename defines that enum value but never assigns it (RNF-11); we actually implement the check.
- **Oil mode does renames only in release one.** oil.nvim supports create/delete/move/cross-directory; we deliberately narrow to renames so the risk surface equals control mode.
- **No tree view.** Following oil.nvim's own judgment (RNF-35): a tree view is a different methodology.
- **No `PowerRenameManager`-style `PeekMessage` pump.** Under Qt this becomes `QEventLoop` plus signals and slots.
- **No hand-rolled two-phase rename with a recovery journal.** Use `IFileOperation` plus the Shell undo stack plus a read-only batch journal.

**Implemented and tested:** the rule core (splitting, the three finalization steps, four case transforms, date/counter/metadata templates, rule chains, oil parsing and diffing, batch collision detection), the `IFileOperation` executor (deepest-first, conflict refusal, no auto-renaming), and dual-mode status agreement (`Presenter`). Evidence: 85 unit cases and 14 real-rename assertions, see the [implementation validation](../WinShun/docs/validation/rename-m0-20261110/IMPLEMENTATION.en.md).

**Unverified:** the host `files` trigger contract and minimum version; the native plugin template/API; whether the shared Qt contract actually works for this plugin (the Qt window code has never been compiled); Qt regex differences from ECMAScript/Boost; Windows file identity and case-only/swap renames; `IFileOperation` behavior over SMB; whether undo actually works; the actual state after a crash; end-to-end metadata reads; accessibility and packaging. The host round-trip (L3) has not run.

## 11. Delivery report (fill after implementation)

This round delivered the pure core, the Windows adapter, the UI logic and the automated tests, and is **not released**: the host round-trip (L3) has not run, and by the rule in this section that is not a completion. Later versions record per-version requirement deviations, the real host/filesystem matrix, L0-L5 evidence, the compatible core subset, actual usage data for both modes, known limitations, and unverified boundaries.
