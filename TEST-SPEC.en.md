# ra9fael.rename local logic verification spec (TEST-SPEC)

**Status: the spec for this round, written before the implementation.** Language links: [中文](TEST-SPEC.md) · [Test plan](TEST-PLAN.en.md) · [Design](plugin-design.en.md) · [UI specification](UI-SPEC.en.md).

Why this file exists next to `TEST-PLAN.md`: the plan covers all the way to L5 (real hardware, SMB, DPI, screen readers, release). **This file defines only what must be verified locally, before the plugin is installed into the host** — L1 (pure core) + L2 (injectable platform edge) + a GUI smoke test. Host integration (L3) is out of scope here.

## 0. Boundary: what this round proves, and what it does not

To prove:

1. **The decision is right** — rule chains, scopes, the three finalization steps, conflict detection and illegal-name rejection all have repeatable local assertions.
2. **The two input modes agree** — a rule chain and an oil buffer produce the same targets for the same files, and reject the same illegal input.
3. **A directory is a set of its entries** — "open the window in this folder" takes the folder's **direct children** as the input set (no recursion, no link following, de-duplicated), instead of treating the folder itself as one item.
4. **The minimal single-file path works** — one file in, one target out; conflicts refused.
5. **The GUI starts and uses the same core** — no host process, no protocol: paths on the command line open the window, and its preview and execution agree with the L1 assertions.

Not proved (explicitly untested here, so nobody assumes otherwise):

- The host's `files` trigger, the shared-Qt contract, elevation, the undo stack, SMB, DPI, screen readers, packaging.

## 1. Layers and pass criteria

| Layer | What | Passes when | Where |
|---|---|---|---|
| **S1 pure core** | `core/`, no Windows, no Qt | `ctest -R rename_core` green | any platform |
| **S2 platform edge** | `win/` enumeration and execution on a temp tree | `ctest -R rename_e2e` green and cleaned up | Windows |
| **S3 view model** | `ui/presenter`, plain C++ | `ctest -R rename_core` (presenter cases ship in the same binary) green | any platform |
| **S4 GUI smoke** | `rename-ui.exe` opens a window directly | window appears, rows and statuses match S1, execution really changes the disk | Windows + Qt |

**Gate**: S1+S2+S3 green before S4; S4 passing before any talk of "install it into the host".

## 2. Cases

IDs are `LS-<layer>-<n>`. Each must be executable, asserted, and leave a file behind. "It looked right" is not a pass.

### S1 pure core: splitting and finalization

| ID | Step | Expect |
|---|---|---|
| LS-S1-01 | `Split("archive.tar.gz")` | name `archive.tar`, extension `gz` |
| LS-S1-02 | `Split(".gitignore")` / `("README")` / `("name.")` | none has an extension; the dot in `name.` is not one |
| LS-S1-03 | `Split` → `Join` | byte-identical |
| LS-S1-04 | `Portion(Extension)` on an extensionless item | empty plus "not applicable", not the whole name |
| LS-S1-05 | `Finalize` trailing spaces and dots | trimmed, `trimmed=true` |
| LS-S1-06 | `Finalize` all blanks / all dots | `ErrorEmptyName` |
| LS-S1-07 | `Finalize` NBSP | becomes a space, `hadNbsp=true` |
| LS-S1-08 | `Finalize` illegal `? * : < > \| "` | `ErrorInvalidCharacter` |
| LS-S1-09 | `Finalize` case-only change | counts as a **change**, not Unchanged |
| LS-S1-10 | `ExceedsLength` by delta | 250-char path, +20 leaf → too long; −20 → fine |
| LS-S1-11 | NFC equivalence (composed vs decomposed) | `NfcEqual` true; `ß` vs `ss` false |
| LS-S1-12 | No normalizer installed | compares code units, does not crash |

### S2 pure core: case and templates

| ID | Step | Expect |
|---|---|---|
| LS-S2-01 | Uppercase / Lowercase / Capitalize / Title Case | all four fixed; Title Case exception words stay lower; `don't` → `Don't` |
| LS-S2-02 | Every date pattern (`$YYYY`…`$fff`, `$TT`, `$tt`, `$$`) | each deterministic; longest match wins; `$$` gives one `$` |
| LS-S2-03 | `$D` beside `$DATE_TAKEN_YYYY`, `$DESCRIPTION`, `$DOCUMENT_ID` | negative lookahead holds; `$D` does not swallow the long keys |
| LS-S2-04 | `$H` beside `$HEIGHT` | same |
| LS-S2-05 | Counter `${start,increment,padding}` | all three honored; huge padding does not explode |
| LS-S2-06 | Counter advances only on a real match | non-matching items take no number; two counters in one item print the same |
| LS-S2-07 | Unknown placeholder | left literal, no error, no silent substitution |

### S3 pure core: rules and plans

| ID | Step | Expect |
|---|---|---|
| LS-S3-01 | Literal first vs all | first only / every occurrence |
| LS-S3-02 | Literal is case-insensitive by default; `FlagCaseSensitive` makes it sensitive | both hold |
| LS-S3-03 | Regex capture group | `(\d+)` → `$1` works |
| LS-S3-04 | Broken regex | `ErrorBadRule`, no crash |
| LS-S3-05 | scope=Extension on an extensionless file and on a folder | not applicable; row Unchanged with a note |
| LS-S3-06 | Each chain step re-splits | `a.b.js`: name step then extension step still yields `a.b.new` |
| LS-S3-07 | Disabled rule | skipped entirely |
| LS-S3-08 | Two rows share a target | **both** flagged `ErrorAlreadyExists` |
| LS-S3-09 | Target exists on disk | `ErrorAlreadyExists` |
| LS-S3-10 | No rule matches | `Unchanged` |

### S4 pure core: oil parsing and diffing

| ID | Step | Expect |
|---|---|---|
| LS-S4-01 | Normal buffer with indentation and a folder's trailing slash | indentation dropped; folder flag right |
| LS-S4-02 | Same id, new leaf | one Rename |
| LS-S4-03 | Lines reordered | no diff |
| LS-S4-04 | Edited id after `/` | that line `UnknownId`; others parse |
| LS-S4-05 | A line vanished | `VanishedId` → a delete, refused this release |
| LS-S4-06 | A line with no anchor | a create, refused |
| LS-S4-07 | ` -> ` link syntax | a link, refused |
| LS-S4-08 | Two names differing only in case | duplicate under filesystem folding |
| LS-S4-09 | One malformed line | row and reason reported; other lines unaffected |
| LS-S4-10 | CRLF and blank lines | tolerated |

### S5 the two modes agree

| ID | Step | Expect |
|---|---|---|
| LS-S5-01 | Same files: rule chain vs oil buffer | identical targets, item by item |
| LS-S5-02 | Same illegal name (`bad?.txt`) in both | both `ErrorInvalidCharacter` |

### S6 view model

| ID | Step | Expect |
|---|---|---|
| LS-S6-01 | Empty input | `Phase::Idle`, `BlockingReason()=="no-files"` |
| LS-S6-02 | Change the filter | visible rows change, **input set does not**; execution order comes from the plan |
| LS-S6-03 | Uncheck a row | plan size unchanged, execution order one shorter |
| LS-S6-04 | After a rebuild | selection survives, by id |
| LS-S6-05 | Stale planId | `AcceptPlan` refuses |
| LS-S6-06 | Batch conflict | `CanExecute()==false`, `BlockingReason()=="errors"` |
| LS-S6-07 | Execution order | grandchild → child → parent |
| LS-S6-08 | After execution | buffer shows the new names, `Phase::Idle` |
| LS-S6-09 | Oil buffer copy → paste | byte-identical round trip, all Unchanged |
| LS-S6-10 | Switching modes with uncommitted edits | `CanSwitchMode` false |

### S7 a directory as the input set (new this round)

| ID | Step | Expect |
|---|---|---|
| LS-S7-01 | `Enumerate{roots:[dir], listContents:true}` | **direct children**, not the folder itself; every depth 0 |
| LS-S7-02 | Same, empty folder | empty result, no error |
| LS-S7-03 | Folder with a subfolder | the subfolder is one row (`isFolder=true`); its contents are **not** listed |
| LS-S7-04 | Folder with a junction / symlink | the link is a row, not entered; a note is left |
| LS-S7-05 | roots = folder and a file inside it | de-duplicated; that file appears once |
| LS-S7-06 | `listContents:false`, root is a folder | the folder itself is the row (previous behaviour unchanged) |
| LS-S7-07 | A root that does not exist | `blocked=true` with a reason |
| LS-S7-08 | `C:\` | direct children, no recursion |
| LS-S7-09 | Rename the folder's contents | files really change; a renamed subfolder leaves its contents' paths alone (all depth 0, no parent/child) |
| LS-S7-10 | roots = one readable, one missing | `blocked=true` and `items` empty (no rows read before the failure) |
| LS-S7-11 | One same-named file from each of two folders, renamed in one batch | both `Ready` (different folders are not duplicates); each is renamed in its own folder |

### S8 the minimal single-file path (new this round)

| ID | Step | Expect |
|---|---|---|
| LS-S8-01 | One file + `find→replace` | one `Ready` row, right target |
| LS-S8-02 | One file, no replace given | `Unchanged`, and a hint to add a replacement (never deletes the found text) |
| LS-S8-03 | One file, target exists | `ErrorAlreadyExists`, `CanExecute()==false` |
| LS-S8-04 | One file, execute | the disk really changes; Shell undo available |
| LS-S8-05 | One file, find text absent from the name | `Unchanged` |

### S9 GUI smoke (local, no host)

| ID | Step | Expect |
|---|---|---|
| LS-S9-01 | `rename-ui.exe --dir <folder>` | window appears, row count = direct children |
| LS-S9-02 | Same, rule `a→b` typed and Enter | rows flip to ready, counts right |
| LS-S9-03 | Click "Rename N" | disk really changes; the window shows the done line |
| LS-S9-04 | `rename-ui.exe --files f1 f2` | two rows, in argument order |
| LS-S9-05 | Switch to text mode | buffer shows `/<id> name`, row count matches the preview |
| LS-S9-06 | Light/dark, interface size | the palette follows (the existing `RENAME_GRAB_WINDOW` hook takes the picture) |

### S10 Host round trip: the row's `data` (new this round)

This is a **protocol contract** case, not a business one. The host hands an item
back as it came (`Items.cpp::itemToJson`), so a field the plugin put in `data`
comes back as `item.data.*`; an `action()` that only looks at the top level
answers "No files selected" to a row that plainly carries `openFolder`. No pure-core
test can catch this — only a protocol round trip.

| ID | Step | Expect |
|---|---|---|
| LS-S10-01 | Host-shaped item (`{data:{openFolder:…}}`) through `action` | takes effect (`hide`, or the reason in a build with no window); **never** "No files selected" |
| LS-S10-02 | Host-shaped item (`{data:{openWindow:true}}`) through `action` | opens an empty window (`hide`), not "No files selected" |
| LS-S10-03 | A `files` trigger with several paths through `action` | the window's input set is exactly those paths; stderr has `files trigger: count=N` and lists each |
| LS-S10-04 | Each row's `data` vs `title`/`subtitle` in `items[]` | the same as `query()` sent, not rewritten by the host |

## 3. Evidence

Every run leaves files under `docs/validation/<topic>-<date>/`:

- `ctest-<layer>.txt` — raw `ctest --output-on-failure` output
- `s7-directory.txt` — S7 and S8 assertion output
- `host-sim-*.txt` — S10 protocol round-trip output (`tools/host-sim.ps1`)
- `gui-<case>.png` — S9 renders (`RENAME_GRAB_WINDOW`)
- `NOTES.md` — what this round did not measure

**A case without an evidence file is recorded as "not run"** and never written up as passing.

## 4. When something fails

1. Decide whether the implementation or the spec is wrong. If the spec is wrong, fix this file, with the date and the reason — never bend the expectation to fit the code.
2. If the implementation is wrong, fix it and re-run that layer **and every layer below it** (an S1 change ripples through S2…S6).
3. If it cannot be fixed, mark the case `not passing` and describe what happens. Never skip.

## 5. The host boundary (not this round, written down to stay aligned)

The plugin's search box keeps exactly two things: **the minimal single-file rename**, and **one row that opens the rename window in the current folder**. Batch work and the oil mode all happen inside the window. Manifest, `files` trigger, shared Qt and packaging are decided only after S1–S9 are green.
