# Local logic verification (2026-11-13)

Spec: [TEST-SPEC.en.md](../../TEST-SPEC.en.md) · Scope: S1–S10.

## Result

| Layer | Cases | Result |
|---|---|---|
| S1–S6 pure core + view model | 86 `rename_core` cases | green (`unit-all.txt`) |
| S7 a folder as the input set | 9 assertions | green (`s7-s8-directory.txt`) |
| S8 the minimal single-file path | 9 assertions | green (`s7-s8-directory.txt`) |
| S2 platform edge (real renames) | 45 e2e assertions | green (`e2e-all.txt`) |
| S9 GUI smoke | 6 cases | green (`gui-s9-*.png`) |
| S10 host round trip (the row's `data`) | 2 paths + a gate | green (`host-sim-*.txt`) |

Totals: `ctest` 4/4 passing, `rename_e2e` **45 passed / 0 failed**, all 86
`rename_tests` cases green.

## S7: a folder as the input set (new)

`EnumerateRequest::listContents` is the only new platform semantic this round: when a root is a folder it stands for **its direct children**, not for itself. That is what "open the rename window in this folder" means.

Measured (`s7-s8-directory.txt`):

- A folder with 2 files and 1 subfolder → 3 rows; the folder itself is **not** one; every depth is 0
- The subfolder's contents are **not** listed (`deep.txt` is absent); the subfolder is a row with `isFolder`
- An empty folder → 0 rows, no error
- `roots` given both the folder and a file inside it → that file appears once (folded-path de-duplication)
- A root that does not exist → `blocked=true` with a reason
- `listContents=false` → the folder itself is still the row (previous behaviour)
- Batch-renaming a folder's contents → 3 items really change; a renamed subfolder leaves `inner.txt` in place (all depth 0, no parent/child reordering)

## S8: the minimal single-file path

- One file + `report→summary` → one `Ready` row, target `single_summary.txt`
- Find text absent → `Unchanged`
- Target exists → `Refused`, source untouched, no `name (1)` created
- Clean rename → the disk really changes

## S9: GUI smoke (local wrapper, no host)

`rename-ui.exe` is the new **standalone window wrapper** (`ui/main-ui.cpp`). It is not the plugin: `plugin.json` names one `runtime.program`, so it is not packaged. Its purpose is to open, drive and screenshot the window on this machine with no host, no protocol and no `.wsplugin`. It links the same `rename_qml` + `rename_win`, so it exercises the plugin's own window path.

```
rename-ui.exe --dir <folder> [--recursive]
rename-ui.exe --files <path> [<path>...] [--recursive]
rename-ui.exe                                  # empty window
```

Evidence:

| File | Case | Result |
|---|---|---|
| `gui-s9-01-dir.png` | `--dir` a folder with 4 files + 1 subfolder | 5 rows, subfolder contents not expanded, footer "5 unchanged" |
| `gui-s9-02-rule.png` | same + rule `IMG_ → photo_` | 4 rows ready, `sub` unchanged, footer "4 to rename", button "Rename 4", **the inputs show IMG_ / photo_** |
| `gui-s9-03-apply.png` | same + execute | disk `draft_*.txt → final_*.txt`; both the table and the footer say "Renamed 3", no contradiction |
| `gui-s9-04-files.png` | `--files beta.txt gamma.txt` | exactly two rows, in argument order, alpha not included |
| `gui-s9-05-text.png` | text mode | buffer `/1 note_1.md` `/2 …` `/3 …`, row count matches |
| `gui-s9-06-textmode-dark.png` | dark fluent palette | theming applies |
| `gui-empty.png` | `rename-ui.exe` with no arguments | empty window; the centred hint and the footer agree |

## The interface (rebuilt on PowerRename's layout)

Mockup: [rename-powertoy-layout.html](../../../../.zcode/v2/visualizations/059d20d2c141065e8cb6a82d2012d6c68ed981e09abe40e46af68091edfa0508/rename-powertoy-layout.html)

One column of rule controls on the left (find, three switches, replace, apply-to, text formatting, time and metadata sources), a two-column table on the right (original → renamed). Four states measured:

| File | State | Result |
|---|---|---|
| `layout-1-empty.png` | just opened, no rule | 4 unchanged rows, primary button disabled |
| `layout-2-preview.png` | `IMG_ → photo_` | 3 rows with bold targets, button reads "Rename 3" |
| `layout-3-conflict.png` | target exists | that row explains in red, others unaffected, status bar warns |
| `layout-4-done.png` | finished | table cleared with the outcome reported, primary button gone |

## Dependency chains and swaps (a real defect fixed this round)

The window showed it: `报告.pdf → 2026报告.pdf` while `2026报告.pdf` is in the same
batch (itself becoming `20262026报告.pdf`). The old code called that "target
exists" and refused — but it is an **ordering** problem, not a conflict.

Fixed in two layers:

1. **Planning** (`core/plan.cpp`): when a target equals another row's *current*
   name and that row is also moving, record `waitsForId` instead of blocking.
   Only "the holder is staying put" or "the waits form a cycle" are refused.
2. **Execution** (`win/execute.cpp`): staged submission. Each round submits only
   the items whose target is free right now, and the next round is planned after
   that one has landed — which is what lets "release then claim" work.

Evidence: `s7-s8-directory.txt` cases LS-S7-10 (a three-item chain all renamed) and
LS-S7-11 (a swap cycle: both refused, both files unchanged). `rename_tests` has 86
cases including `PlanWaitsForTheRowThatFreesItsTarget`.

## Plugin test flow (the host runner's environment)

`plugin-host-roundtrip.txt`: the plugin was started with exactly the environment
`WinShun/src/plugin/runner/Sandbox.cpp::withHostQt` sets (`PATH` prefixed with the
host folder, `WS_QT_DIR`, `QT_PLUGIN_PATH`, `QT_QPA_PLATFORM_PLUGIN_PATH`,
`QML_IMPORT_PATH`, `QT_QUICK_CONTROLS_STYLE=Basic`) and driven through
`initialize` → `query` with `filters.folder`.

- initialize answered in 44 ms (the manifest allows 1500)
- query returned two rows; the second carries `openFolder`
- no stderr at all

`tools/host-sim.ps1` now completes both paths; the evidence is in
`host-sim-folder.txt` and `host-sim-one-file.txt`:

| Path | Assertion | Result |
|---|---|---|
| folder row | answers `hide` (a window really opened), never "No files selected" | passing |
| one-file row | answers "Renamed 1", and `report.txt` really became `summary.txt` on disk | passing |

## The row's `data` (the second real defect fixed this round)

The host hands the item **back as it came**
(`WinShun/src/plugin/Items.cpp::itemToJson`), so a field the plugin put in `data`
comes back as `item.data.*`. But `action()` read the top level —
`a.item["batch"]` — and therefore answered **"No files selected"** to a row that
plainly carried a batch.

It does not fail loudly: it looks like the UI did nothing, when in fact the wrong
level was read. No pure-core test can catch it — only a protocol round trip.

Fixed by taking `payload = a.item["data"].isObject() ? a.item["data"] : a.item`
first and reading the fields from there, keeping the top-level fallback so the
once-plugin shape still works. `tools/check-action-payload.ps1` is now a ctest
gate (`action_payload`), negative-tested: changing `payload["batch"]` back to
`a.item["batch"]` fails it immediately.

Four new cases (LS-S10-01…04 in `TEST-SPEC.md`) cover both shapes and the host not
rewriting what a row carries.

## A defect in the script itself

`tools/host-sim.ps1` used to **hang**. PowerShell variable names are
case-insensitive, so the parameter `[string]$Query` and the script's `$query` were
one variable: assigning a hashtable to it coerced straight back to a string, and
what went out was `"System.Collections.Hashtable"`. The plugin could not parse it,
so the script sat in `ReadLine()` forever.

Fixed by renaming the parameter to `$QueryText` and the message to `$queryMsg`,
and by replacing every `ReadLine()` with `Read-Line` (a timed
`ReadLineAsync().Wait()`): on timeout it kills the process and records a FAIL
instead of hanging silently.

Package: `dist/ra9fael.rename-0.1.0.wsplugin`, sha256 `911fbdc9…e9f00`, 214,707
bytes; dumpbin confirms the packaged exe imports Qt6Core (the shared-Qt contract).

## Fixed this round

1. **`listContents` path joining**: the `FindFirstFileW` pattern lacked its separator (`C:\dir*` instead of `C:\dir\*`), so folder enumeration came back empty — found after 4 assertions failed; drive roots (`C:\`) handled too.
2. **A missing root did not report**: it returned an empty set silently; it now blocks with "not found: …".
3. **The footer contradicted the table**: the table said "Renamed 3" while the footer said "No files yet". `doneLine` now suppresses the blocking reason and hides the "Rename N" button (nothing is left to run, and a disabled button reads as a failure).
4. **The rule inputs did not fill in**: when a rule was set from outside (the wrapper's command line, a future preset) the four inputs stayed empty while the preview had clearly used the rule. `ruleFind/ruleReplace/ruleScope/ruleMatch/ruleEnabled` are now Bridge properties with two-way QML binding.

## The search box, narrowed (as requested)

The box keeps exactly two things; batch work and the oil mode live in the window:

| Input | Result |
|---|---|
| `rename` | two rows: `Rename one file: rename old -> new`, `Rename in a folder…` |
| `rename report -> 报告` | a **single** preview row when exactly one file matches; with several it says "N files match, the box renames one file" |
| `rename report` | `A new name is needed`, suggesting `rename report -> newname` |

Protocol transcript: `keyword-modes.txt`.

## Not verified (out of scope, written down so it is not assumed)

- **Clicked through in the real WinShun window**: the `files` trigger, a real `appearance` change, elevation, the Shell undo stack, SMB, DPI and screen readers were never driven by hand in the host's GUI. The host runner's **environment and protocol round trip** are verified (`host-sim-*.txt`), but that is not the same as clicking in the window.
- **S9 covers the wrapper path only**: the window inside the host renders from the same `Main.qml` + `Bridge`, but that process boundary, threading model and permission set were not exercised.
- **`--recursive` was code-reviewed only**, with no e2e assertion.
- **UI interaction details** (clicking a checkbox, drag-select, scrolling) are not automated; S9 verified the data flow and the rendering.
- **light / brand themes**: only dark fluent was checked, as asked.
- **`.wsplugin` installation**: the package is built and its contents verified, but it did not go through the host's installer (this round copied the exe into the plugin folder directly).

## Evidence files

- `ctest-all.txt` — raw `ctest --output-on-failure` (4 tests)
- `unit-all.txt` — the 86 `rename_tests` cases
- `e2e-all.txt` — the 45 `rename_e2e` assertions (S7/S8/S10 included)
- `s7-s8-directory.txt` — the detailed S7/S8 assertion output
- `keyword-modes.txt` — the three search-box paths, protocol level
- `host-sim-folder.txt` — the folder row: `hide`, with the "No files selected" negative assertion
- `host-sim-one-file.txt` — the one-file row: really renamed to `summary.txt`
- `plugin-host-roundtrip.txt` — initialize/query round trip in the host environment
- `layout-1-empty.png` … `layout-4-done.png` — the four PowerRename-layout states
- `gui-s9-01-dir.png` … `gui-s9-06-textmode-dark.png` — window renders
