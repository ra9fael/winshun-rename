# ra9fael.rename M0 validation and manual acceptance

[中文](README.md)

Date: 2026-11-10. Status: M0 probes and skeleton; **not a releasable plugin**. Curated machine-readable evidence lives in the workspace host repository at `WinShun/docs/validation/rename-m0-20261110/`.

## 1. What the problem was

The `ra9fael.rename` design (`plugin-design.md`) and development plan (`DEVELOPMENT-PLAN.md`) rest on six **unverified assumptions**. If any one fails the architecture changes, and the later it surfaces the higher the cost. M0 exists to test those six before product code is written, and to stand up a skeleton that compiles and tests.

Environment: Windows x64, MSVC 19.51.36260.0 (VS 18 Community), CMake 3.29.2, Ninja, PowerShell 7.

## 2. Why

All six assumptions are "the design depends on it, but nothing has measured it":

| Assumption | If it fails |
|---|---|
| P1 the host `files` trigger reaches the plugin | The entry plan collapses; the product shape has to be redone |
| P2 shared Qt works for this plugin | Must bundle Qt; the package grows significantly |
| P3 `IFileOperation` can rename inside the plugin process | The execution model (RNF-14/15) changes wholesale |
| P4 how the elevation prompt actually appears | Renaming under protected directories becomes undefined |
| P5 the 32000-character `files` cap | The large-selection strategy has to change |
| P6 WIC metadata readable inside the plugin | Metadata templates (RN-FR-16) must be degraded |

Testing first avoids "build it, then discover the foundation is wrong".

## 3. What changed

New content in the standalone plugin repository; **no host program code was modified**:

- `core/`: the pure rule core (`rename_core.h` plus 7 implementation files). Hard constraint: **no Windows or Qt headers** (development plan §4).
- `tests/`: the workspace-standard zero-dependency `test_harness.h` and 32 L1 cases.
- `probe/probe_main.cpp`: the M0 probe entry, serving the SDK protocol and logging the `files` trigger payload to stderr (P1).
- `package/plugin.json`: the probe manifest (v0.0.1, `files` trigger plus `ui`/`files:read`/`files:write`).
- `tools/check_core_purity.ps1`: the `core/` purity gate, registered with ctest.
- `CMakeLists.txt`: C++20, CMake 3.21+, x64 enforced, static CRT (matching `findmouse`).

## 4. Measured

| Layer / design scope | Method | Result |
|---|---|---|
| Build | `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release` plus `cmake --build` | Passes; produces `rename.exe` (probe) and `rename_tests.exe`; no errors, no warnings (`/W4 /permissive-`) |
| L1 pure core | `ctest --test-dir build-msvc` → `rename_core` | **32/32 pass.** Covers scope splitting (`archive.tar.gz`/`.gitignore`/`README`/trailing dot), NFC equivalence and `ß`≠`ss`, trim boundaries (all-whitespace, NBSP, invalid characters), the four case transforms (including Title Case exceptions and apostrophe contractions), counter padding and the huge-padding guard, and plan status aggregation |
| Architectural gate | ctest `core_purity` | Passes; also **negative-tested**: temporarily adding `#include <windows.h>` made the gate exit 1 and name the file and line |
| Manifest | JSON parse plus comparison against `plugin.schema.json` | Valid; `id`/`version`/`triggers.files`/`permissions` all match the schema enums |

Evidence files:

- `ctest.txt` — full output of both tests, all 32 cases listed OK
- `manifest-check.txt` — manifest field check

### 4.1 Three real bugs the tests caught

All three are implementation errors stopped by L1 immediately, not design problems:

1. **`FormatCounter` returned an empty string for every value.** The format was built as `"%%ll"`, missing the `d` conversion, so `swprintf` failed outright. Fixed to `"%lld"` / `"%0<width>lld"`.
2. **Title Case split `don't` into two words**, producing `Don'T`. Fixed so an apostrophe counts as a boundary only at a word start or after a separator.
3. **`Capitalize` judged separators per character too**, same root cause as (2). Fixed together.

### 4.2 Current state of the six assumptions

| Assumption | State | Basis |
|---|---|---|
| P1 `files` trigger | **not measured** (code in place) | The probe compiles and serves the protocol; it still has to be triggered from the result menu in the real host |
| P2 shared Qt | **not measured** | An M3 prerequisite; untouched this phase |
| P3 `IFileOperation` | **not measured** | An M2 prerequisite |
| P4 elevation prompt | **not measured** | Same |
| P5 array cap | **not measured** (the probe logs the count) | The probe already prints `files=N chars=M`; needs a real selection |
| P6 WIC metadata | **not measured** | The last item in M5 |

**Honest conclusion: M0 delivered only the skeleton, the pure core and the gate — not one of the six assumptions has been verified yet.** The plan says "P1–P3 must all pass before M1/M2", so the next step is installing the probe in the real host and running P1.

## 5. Not measured

- **No host round-trip at all**: the probe was not installed, started or triggered. Whether the `files` trigger really arrives, how the `files` cap behaves, and whether stderr logs reach disk are all unverified.
- **No Qt at all**: the M3 shared Qt contract, the `appearance` message, and any QML window are untouched.
- **No `IFileOperation` at all**: deepest-first ordering, identity re-checks, the elevation prompt, and SMB behaviour are unverified. `win/` and `ui/` are empty directories.
- **The regex path is unimplemented**: the literal branch of `RuleEngine` works, the regex branch returns `RuleError::Unsupported`. That is deliberate — a half-built engine must not be mistaken for one that can rename.
- **Templates implement only `$$` escaping and the counter placeholder**: the date pattern table, negative lookahead, and `${start=...,increment=...}` parsing are TODOs.
- **Oil mode has only the parser and the differ**: no buffer, no UI, no id-table sync. The duplicate-name check in `OilDiffer` also lacks a case covering `UnknownId` end to end.
- **Every "validated on Windows/SMB" claim is still empty**: no L3 or L4 case has run.
- The x86 build rejection was not actually exercised (only the `CMAKE_SIZEOF_VOID_P` check exists; a 32-bit configure was never attempted).
