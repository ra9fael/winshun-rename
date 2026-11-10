# ra9fael.rename Test Plan (Proposal)

**Status: some cases executed.** Bilingual links: [中文](TEST-PLAN.md) · [README](README.en.md) · [Design](plugin-design.en.md) · [UI specification](UI-SPEC.en.md).

## Execution progress

The cases below have automated evidence and are now "executed"; the rest remain "not run". The full measured record is in the [implementation validation](docs/validation/rename-m0-20261110/IMPLEMENTATION.en.md).

| Executed | Evidence |
|---|---|
| RN-T1-01…17 (pure core, expanded to 85 finer cases) | `ctest` → `rename_core` all green |
| RN-T2-01/04/05/06/07 (fake-layer behaviour) | presenter cases inside `rename_core` |
| RN-T2-02/03 (executor failure paths) | some `rename_e2e` assertions plus code review |
| RN-T6-01 (conflicts never auto-renamed) | `rename_e2e` asserts `no automatic 'name (1)' was created` |
| RN-T6-02/03/05/08 | unit cases |
| RN-T6-04/06/07 | true in code, no runtime assertion |

**Not run**: all of L3 (real host), all of L4 (device/SMB/DPI/screen reader), all of L5, and the host-side checks in RN-T0-01/02.

## Test layers and bookkeeping

L0 manifest/package static checks; L1 pure rule core; L2 fake filesystem/host dependency injection; L3 real WinShun host round-trip (completion gate); L4 Windows/SMB interactive E2E; L5 calibration and release. Every status field must record "not run" or a measured conclusion; when executed, fill in the command, environment, and evidence file name. Never write a plan as a result. Quantified budgets are marked untested and do not imply a pass.

## Case matrix

| ID | FR/design link | Layer | Precondition | Steps | Expected | Evidence | Status |
|---|---|---|---|---|---|---|---|
| RN-T0-01 | RNF-05, RN-FR-01, §3 | L0 | Host entry and manifest approved | Validate schema, ID/folder, version, runtime, permissions, `files` trigger capability/minimum host version, bilingual fields | Schema matches the host contract; no unsupported Explorer claim | Validation summary, host-version matrix | Not run |
| RN-T0-02 | §8 | L0 | Package rules defined | Build a clean package; scan extra binaries, logs, path data, secrets, dependency licenses/notices | Only approved files included; no secrets; dependency provenance and licenses traceable | Package inventory, license summary, secret-scan summary | Not run |
| RN-T1-01 | RN-FR-02/03, §3.5 | L1 | Pure C++ rule core runnable | Golden inputs for name/extension/full with first/all and literal/regex | Scopes and occurrence exact; extensionless, multi-dot names and directories fixed; chain order deterministic | Unit output, fixture version | Not run |
| RN-T1-02 | RN-FR-08/11, §3.7 | L1 | Compatibility subset defined | Compare external-tool documentation cases: captures, first/all, counter start/increment/padding, dates | Only explicitly declared subset cases agree; unsupported syntax is clearly refused; never claim Qt regex equals ECMAScript/Boost | Dual-engine case table, difference list | Not run |
| RN-T1-03 | RN-FR-04/05/10, §4 | L1 | Preview plan/conflict logic exists | Input unmatched, unchanged, invalid names, in-batch duplicates and existing targets; change rules/selection | Highlights and targets match core output; errors block; stale previews die; no overwrite | Pure-core case report | Not run |
| RN-T1-04 | RN-FR-07, §7 | L1 | Windows name validation exists | Test CON/PRN/AUX/NUL, COM/LPT, trailing dots and spaces, invalid characters, UTF-16 length, Unicode combining | Windows reserved names consistently refused; no unrequested Unicode normalization; path length follows target policy | Case output, target-platform notes | Not run |
| RN-T1-05 | RN-FR-08 | L1 | Template parser exists | Counter boundaries, negative/zero increment policy, overflow, padding, invalid/unknown placeholders, missing date metadata | Deterministic results; invalid input is an error rather than a silent substitute; no expression evaluation | Unit report | Not run |
| RN-T1-06 | RN-FR-02, §3.5 | L1 | Scope core exists | Apply name/extension/full to `archive.tar.gz`, `.gitignore`, `README`, and directory `a.b`; extension regex matching the empty string on an extensionless file; chain full then extension | `archive.tar.gz` splits to `archive.tar`/`gz`; `.gitignore`/`README` have no extension and extension rules skip with explanation; directory name/full is `a.b` and extension is inapplicable; re-split each step and name keeps the suffix | Golden cases and output | Not run |
| RN-T1-07 | **§3.7, RNF-21** | L1 | Date template engine exists | Verify each of `$YYYY $YY $Y $MMMM $MMM $MM $M $DDDD $DDD $DD $D $HH $H $hh $h $mm $m $ss $s $fff $ff $f $TT $tt` plus `$$` escaping | Each pattern yields a deterministic result; longer patterns precede shorter ones; `$$` emits one `$` | Per-pattern golden table | Not run |
| RN-T1-08 | **§3.7, RNF-21** | L1 | Same | Test `$D` beside `$DATE_TAKEN_YYYY`, `$DESCRIPTION`, `$DOCUMENT_ID`; `$H` beside `$HEIGHT` | Negative lookahead holds: `$D`/`$H` do not swallow the longer pattern prefixes; bad combinations are either refused or produce stable results | Collision-pair table | Not run |
| RN-T1-09 | **§3.6, RNF-19** | L1 | Finalization exists | Feed original names in NFC and NFD forms with equivalent and non-equivalent targets; `ß` vs `ss`; `Straße` vs `STRASSE` | NFC-equivalent pairs are Unchanged; `ß` differs from `ss`; a case-only change counts as a change (not Unchanged) | Unicode case table | Not run |
| RN-T1-10 | **§3.7, RNF-16** | L1 | Counter index implemented | Three items where the middle one does not match; then two `${}` in one chain; then start/increment/padding combinations | Unmatched items consume no number; two templates in one item print the same number; the index continues across items without reset | Counter-sequence assertions | Not run |
| RN-T1-11 | **§3.6** | L1 | Finalization exists | Input all-whitespace, all-dots, mixed trailing spaces and dots, leading whitespace, and an NBSP | No crash and no unsigned index underflow; an empty trimmed result is invalid; NBSP becomes a plain space and returns a "handled" marker | Boundary case output | Not run |
| RN-T1-12 | **§3.6, RNF-17** | L1 | Case transforms exist | Run Uppercase / Lowercase / Title Case (including a/an/the/of exceptions and apostrophe contractions) / Capitalize samples, plus directories | Four behaviours are fixed; the Title Case exception list applies; contractions are not split wrongly; directories follow the same rules | Transform comparison table | Not run |
| RN-T1-13 | **§3.8, RN-FR-18** | L1 | `ParseOilBuffer` exists | Normal buffer with indentation and trailing-slash directories; same id with a new name; changed line order | Indentation discarded; directories flagged; same-id rename yields Rename; order changes yield no diff | Parse-result table | Not run |
| RN-T1-14 | **§3.8, RN-FR-18/19** | L1 | Same | Edited `/<id>`, unknown id, id-less new line, vanished line, ` -> ` link syntax | Each becomes the matching error line while the rest parse normally; Delete/Create/Link never reach the action layer | Error code and line-number table | Not run |
| RN-T1-15 | **§3.8, RNF-31** | L1 | Duplicate detection exists | `Report.txt` and `report.txt` in one batch; `a.txt` and `a.TXT` | Normalized by FS semantics, duplicates are refused and both lines marked; case differences do not slip through | Duplicate-check output | Not run |
| RN-T1-16 | **§3.8** | L1 | Same | Leaf containing `/` or `\`, empty leaf, invalid characters, over-length names, existing targets | Each is refused with a specific reason and never enters the execution plan | Rejection-reason table | Not run |
| RN-T1-17 | **§4, RNF-25** | L1 | Regex core and oil parser exist | Fuzz: three-way split for regex (following the PowerRename.FuzzingTest structure) plus random text into `ParseOilBuffer` | No crash, no out-of-bounds access, no infinite loop; error line numbers never exceed the buffer; no undefined behaviour | Fuzz run log, corpus seeds | Not run |
| RN-T2-01 | RN-FR-01/04, §3/§5 | L2 | Fake host and filesystem exist | Inject empty/duplicate/stale selections; simulate filtering, sorting, selection and rule changes | No traversal while recursion is off; filtering never changes the set; selection only affects the plan; async results never pollute a newer preview | Fake call counts, event trace | Not run |
| RN-T2-02 | RN-FR-06/07, §4 | L2 | Injectable rename backend | Inject name swaps, case-only renames, per-phase failures, cancellation, external replacement | No overwrite; failures reported per item; partial success never reported as a full success | File identity/path trace | Not run |
| RN-T2-03 | RN-FR-06, §4/§7 | L2 | Fake `IFileOperation` interface exists | Simulate elevation denial, SMB disconnect, partial success, cancellation at safe boundaries | Per-item states distinguishable; no silent retry; never claim rollback | Fault-injection log | Not run |
| RN-T2-04 | RN-FR-14/15, UI §2-3 | L2 | Fake traversal/rename interfaces exist | Default off, explicit on, toggle off and on; nested/overlapping roots, link cycles, escapes, read failures | Zero traversal when off; when on, collect only files under the roots, deduplicated, no link following, no automatic directory renames; failures block an incomplete set; toggling invalidates preview | Traversal/write call counts, plan | Not run |
| RN-T2-05 | **RN-FR-15, §4.3** | L2 | Fake depth model exists | Supply a parent, child and grandchild directory plus files under them, all renamed | Submission order is deepest-first; subtree paths are fixed after the parent renames; preview surfaces the parent/child relation | Submission-order trace, path-fix assertions | Not run |
| RN-T2-06 | **RN-FR-20** | L2 | Both modes can emit plans | Drive the same file set through control mode and oil mode to the same final names | Both modes assign the same status (Ready/Unchanged/Error/Exists/TooLong) to the same name; finalization notices agree | Dual-mode comparison table | Not run |
| RN-T2-07 | **RN-FR-19** | L2 | Oil parser exists | Buffer containing both rename lines and delete/create lines | The whole batch is refused; no partial rename-only execution; the error identifies the out-of-scope lines | Refusal log, write-call count of zero | Not run |
| RN-T3-01 | RN-FR-01, §3/§5 | L3 | A specific WinShun build with the plugin and host `files` entry available | Open from a host selection; check input IDs/paths; close and reopen | The entry receives only host-selected items; no Explorer claim; host message records show a correct round-trip | Message records, selection checklist | Not run |
| RN-T3-02 | RN-FR-04/05/08, §5 | L3 | Isolated test directory and an approved build | Configure a rule chain, preview, filter, select, execute | UI preview matches disk results; unselected/unmatched items unchanged; conflicts never overwrite | Before/after listings, host log, screenshots | Not run |
| RN-T3-03 | RN-FR-06, §4 | L3 | A volume with stable file identity | Undo after a finished batch; undo after the original name is occupied; undo after the target was modified | Normal undo succeeds through the Shell undo stack; later files are never overwritten; changed objects are refused with the batch journal retained | Directory snapshot, undo record | Not run |
| RN-T3-04 | RN-FR-14/15, UI §2-3 | L3 | Real host and an isolated parent/child tree | Confirm default off; enable collection and verify the summary; rename a parent together with descendants | Roots/counts/provenance correct; directories are not renamed implicitly; parent/child renames run deepest-first and the tree matches the preview | Host records, before/after trees, screenshots | Not run |
| RN-T3-05 | **RN-FR-18, UI §3** | L3 | Real host and an isolated directory | Edit leaf names in text mode, pre-check, `Ctrl+S`, confirm, execute, observe the buffer reset | Preview equals disk results; the buffer reflects new names and new ids afterwards; error lines block submission | Host log, before/after listings, screenshots | Not run |
| RN-T4-01 | RN-FR-07, §7 | L4 | Windows 10/11 x64 with multi-volume and real-user directories | Name swaps, case-only, Unicode, reserved names, parent/child selection, over-length names, external edits | Matrix behaviour matches platform policy; no overwrite or wrong-object operations; unsupported cases clearly refused | OS build, screenshots, listings, event log | Not run |
| RN-T4-02 | RN-FR-06/07, §4/§7 | L4 | SMB share, disconnect injection, concurrent external renaming | Swap/bulk rename over SMB; interrupt transport; cancel; reconnect | Claim only measured semantics; disconnects never misreported; dangerous operations never replayed automatically | SMB config, server and client logs, before/after listings | Not run |
| RN-T4-03 | RN-FR-12, §6 | L4 | AI absent from release one | Statically audit network permissions and calls | No AI network path in release one; any future candidate output must be re-validated locally and confirmed | Static scan/network interception record | Not run |
| RN-T4-04 | UI §4, RN-FR-04/06 | L4 | 100/150/200% DPI, keyboard, screen reader, light/dark/high-contrast | Complete preview through execute/cancel/undo without a mouse; inspect long names and error states | No clipped or overlapping key fields; focus and state accessible; Enter does not bypass confirmation | Screenshots, keyboard log, accessibility check | Not run |
| RN-T4-05 | **UI §3.1-3.2, RN-FR-18** | L4 | Same | Text mode: caret skips `/<id>`; paste a block from elsewhere; error rows identifiable without color; narrow window scrolls horizontally | `/<id>` is not editable; pasting produces create errors with a clear notice; errors use color plus text; no wrapping | Screenshots, recording, accessibility check | Not run |
| RN-T5-01 | RN-FR-13, §3/§8/§10 | L5 | Explorer exploration approved as separate scope | Verify registration/unregistration, user privilege, Windows versions, host lifecycle and failure recovery | Recorded independently; no Explorer integration claim unless the host contract is met | Exploration report, version matrix, rollback evidence | Not run |
| RN-T5-02 | RN-FR-06/07, §9-11 | L5 | Required L0-L4 items pass | Install on a clean profile; assess performance, memory, post-crash state, licenses | Record device, file set, filesystem; L3 passes; every claim has evidence | Release checklist, raw summary, dependency/license inventory | Not run |

## Behavioural differences from the borrowed references (regression guards)

These cases exist to confirm we **deliberately deviate**; any drift toward the reference behaviour is a failure.

| ID | Reference | Expectation | Status |
|---|---|---|---|
| RN-T6-01 | PowerRename `FOF_RENAMEONCOLLISION` | Existing targets are **refused**; never auto-renamed to `name (1)` | Not run |
| RN-T6-02 | PowerRename `ItemNameAlreadyExists` dead code | We **actually implement** target-exists detection; a constructed conflict must yield `Exists`, never `Ready` | Not run |
| RN-T6-03 | PowerRename single replacement | Our rule chain supports multiple levels, re-splitting name/extension/full at each level | Not run |
| RN-T6-04 | PowerRename self-built recovery | We keep no recovery journal; after a crash we never auto-roll-back and instead point at the Shell undo stack | Not run |
| RN-T6-05 | oil.nvim full operations | Oil mode is **renames only**; create/delete/move/link are all refused | Not run |
| RN-T6-06 | oil.nvim tree view | We **do not** implement a tree view; indentation is visual only | Not run |
| RN-T6-07 | oil.nvim `skip_confirm_for_simple_edits` | We **always confirm**; no skip-confirmation switch is offered | Not run |
| RN-T6-08 | PowerRename trailing-dot trim | We trim but **explicitly report** "trailing whitespace/dot removed", never silently | Not run |

## Budgets to measure (not achieved targets)

Measure rather than promise: preview core p95 response for 10,000 items; oil-mode pre-check duration for a 1,000-line buffer; peak memory; total elapsed time to execute 1,000 items. Concrete numbers must be measured on the target machine before being recorded; never pre-write them as conclusions.

## Unexecuted items

Every case is currently "not run". There is no implementation, build, fuzz run, host round-trip, or device validation. Every "expected" in this table is a design requirement, not a measured result.
