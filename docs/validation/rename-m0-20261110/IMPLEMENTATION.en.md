# ra9fael.rename implementation validation (M1–M5)

[中文](IMPLEMENTATION.md)

Date: 2026-11-10. Covers M1 (pure core), M2 (Windows adapter), M3/M4 (UI layer) and M5 (metadata plus finishing). **None of M0's six host assumptions has been verified yet** — see the [M0 validation](README.en.md).

## 1. What the problem was

M0 only stood up a skeleton; not one of the six key assumptions had been tested. This phase implements and tests everything the design can prove on its own — the rule core, the executor, the dual-mode UI logic — while keeping what it cannot prove (host round-trip, the Qt window, the elevation prompt) explicitly unverified.

Environment: Windows x64, MSVC 19.51.36260.0, CMake 3.29.2, Ninja, PowerShell 7. **Qt is not installed on this machine** (no `Qt6Config.cmake`, no `Qt6Core.dll`, no vcpkg), so the Qt window ships as an optional target only.

## 2. Why

Part of the design is fully self-proving: given rules and filenames the final name is deterministic; given a batch the execution order is deterministic; given a conflict it must be refused. None of that needs a host, Qt or a real device — pure logic proves it. Making that part solid is what shrinks the remaining uncertainty.

Conversely, whether `IFileOperation` can rename inside the plugin process, whether the host's `files` trigger really arrives, and whether the shared Qt contract works **require** a host environment. This phase does not pretend otherwise.

## 3. What changed

| Layer | Files | Content |
|---|---|---|
| `core/` | 10 files | Pure core: splitting, the three finalization steps, four case transforms, date/counter/metadata templates, rule chains, the oil parser, the differ, two plan builders. **Zero Windows/Qt headers** |
| `win/` | 6 files | Enumeration, `IFileOperation` executor, NFC injection, template context, WIC metadata |
| `ui/` | 4 files | `Presenter` (plain C++, testable), `Bridge` (Qt adapter, optional), `Main.qml`, `window_main.cpp` |
| `tests/` | 3 files | 85 unit cases, 14 end-to-end assertions, the zero-dependency harness |
| `tools/` | 1 file | The `core/` purity gate, registered with ctest |
| root | `main.cpp` | The resident plugin entry point, serving the SDK protocol |

No host program code was modified.

## 4. Measured

| Item | Method | Result |
|---|---|---|
| Build | Ninja plus MSVC 19.51, `/W4 /permissive-` | Passes, **zero errors, zero warnings** |
| L1 pure core | `ctest` → `rename_core` | **85/85 pass** |
| L2/L3 end-to-end | `ctest` → `rename_e2e` | **14/14 assertions pass**, real renames on disk |
| Architectural gate | `ctest` → `core_purity` | Passes; negative-tested (inserting `#include <windows.h>` is caught with file and line) |
| Qt-off path | `cmake -DRENAME_BUILD_QT=OFF` (default) | Configures and builds; prints the `RENAME_BUILD_QT=OFF` notice |

Evidence: `ctest-implementation.txt` (full output), `e2e-rename.txt` (real rename log).

### 4.1 What the end-to-end run actually proves

`e2e-rename.exe` runs against a temp tree and passes all 14 assertions:

- Enumeration finds both items and is not blocked
- The plan marks both rows `ready`
- **Both files really renamed on disk**, and the originals are gone
- An existing target is **refused**; the target is untouched, the source is not renamed, and **no `taken (1).txt` is created** — the direct evidence that PowerRename's `FOF_RENAMEONCOLLISION` behaviour was not copied
- With a parent directory and its child in one batch, **the child renames first and the parent second**, and the child ends up under the renamed parent

### 4.2 Ten implementation bugs the tests caught and fixed

All implementation errors, not design problems, each stopped by a case:

| # | Bug | Effect | Fix |
|---|---|---|---|
| 1 | `FormatCounter` built `"%%ll"` | Every counter returned an **empty string** | Use `"%lld"` / `"%0<width>lld"` |
| 2 | Title Case split `don't` into two words | Produced `Don'T` | An apostrophe is a boundary only at a word start or after a separator |
| 3 | Same root cause in `Capitalize` | Same | Fixed together |
| 4 | Oil parser checked separators before stripping the trailing slash | Directory row `/1044 photos/` reported as an invalid path | Reorder: link → strip slash → check interior separator |
| 5 | The `-> ` link check ran after the separator check | Link lines got the wrong error kind | Move the link check first |
| 6 | Counters numbered from 0 | Users saw `pic-0` | `value` starts at `start` (1 by default) |
| 7 | Batch collisions marked only one row | The other row still showed `ready` and would be missed | Two-pass: snapshot `Ready`, then mark both |
| 8 | `Rebuild()` restored selection after rebuilding | `m_rows` was already cleared, so checkboxes reset | Snapshot selection **before** rebuilding |
| 9 | `$D` swallowed `$DATE_TAKEN` | The metadata key could never expand | Negative lookahead covers the no-underscore form |
| 10 | Mixed CRT (static exe, dynamic static libs) | LNK2038 | All targets use `MultiThreaded` |

## 5. Not measured

**This section is not omitted.**

### 5.1 Host-side: nothing at all

- The `files` trigger has **never fired in a real host**. `main.cpp` compiles and serves the protocol, but was never installed and never invoked from the result menu.
- The real behaviour of the 32000-character `files` cap is unknown.
- The shared Qt contract (`runtime.qt: "host"`) is **completely unverified**, because Qt is absent here.
- How `IFileOperation`'s elevation prompt (`FOFX_SHOWELEVATIONPROMPT`) actually appears inside the plugin process is unknown.
- The permission declarations `ui` / `files:read` / `files:write` were only compared against the schema; the real host consent flow was never exercised.

### 5.2 The Qt window: written but never compiled

`ui/bridge.cpp`, `ui/qml/Main.qml` and `ui/window_main.cpp` have never been compiled — there is no Qt on this machine. They **may contain compile errors**. `RENAME_BUILD_QT=ON` is the only entry point, and Qt 6.5+ must be installed first.

The QML rule editor has a single rule row. The chain editor, flag panel, the read-only `/<id>` prefix and the line gutter all remain for after M5.

### 5.3 Explicitly unverified behaviour

- **Identity re-check limits**: `execute.cpp` compares size/time/volume serial/file index, but cannot detect a replacement with identical size and timestamp. A known limitation, not adversarially tested.
- **SMB**: never run against a share. Disconnects, `IFileOperation` semantic differences and temporary-name behaviour are all unknown.
- **Undo**: `FOF_ALLOWUNDO | FOFX_ADDUNDORECORD` is set, but **Ctrl+Z was never verified to work**.
- **Name swaps and case-only renames**: `IFileOperation`'s actual behaviour is unknown.
- **Metadata**: `metadata.cpp` reads EXIF through WIC, but there is **no test image**, so not one metadata assertion has run end to end. Only template expansion with injected maps is covered.
- **Recursive collection**: the recursion branch in `Enumerate` has never executed (`main.cpp` passes `recursive = false`, and the e2e only tests a flat set).
- **x86 rejection**: only the `CMAKE_SIZEOF_VOID_P` check exists; a 32-bit configure was never attempted.
- **Accessibility, DPI, theming**: untested, because the window never ran.

### 5.4 Behavioural differences from the references: partially verified

`RN-T6-01` (conflicts are refused, never auto-renamed) **is verified by the e2e**, the most concrete item this phase produced. The other seven have unit-level evidence or none:

| Case | State |
|---|---|
| RN-T6-01 conflict refused, no automatic `(1)` | **Verified by e2e** |
| RN-T6-02 `ItemNameAlreadyExists` really implemented | Unit-covered (`PlanDetectsTargetAlreadyExists`) |
| RN-T6-03 multi-level rule chain | Unit-covered (`ChainResplitsAfterEachRule`) |
| RN-T6-04 no recovery journal | True in code; no runtime assertion |
| RN-T6-05 oil mode renames only | Unit-covered (`OilDifferRejectsVanishedId` etc.) |
| RN-T6-06 no tree view | No UI, untested |
| RN-T6-07 always confirm | No UI, untested |
| RN-T6-08 trim reported explicitly | Unit-covered (the `trimmed` flag) |

## 6. Conclusion

**What can be claimed**: the rule core, conflict refusal, deepest-first execution, dual-mode status agreement, and oil parsing/diffing all have reproducible automated evidence.

**What cannot be claimed**: that the plugin works in a real host. Entry, permissions, the Qt window, elevation, undo, SMB, metadata and recursion — not one of those eight has been measured.

The next step requires someone with a host environment to run P1: install `rename.exe`, trigger it once from the result menu, and check stderr for the `files` payload. Until then, "it can rename" is only true of a temp directory.
