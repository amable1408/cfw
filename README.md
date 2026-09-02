# CFW

[![CI](https://github.com/amable1408/cfw/actions/workflows/ci.yml/badge.svg)](https://github.com/amable1408/cfw/actions/workflows/ci.yml)

A modular **C23 framework**: self-contained modules, one translation unit each, a paired header
per module, and a test suite per module. This repository is the **audited export** of CFW - it
holds only the modules that have cleared every gate below, together with everything they include.

Version **0.1.0** ships **14 modules** (109 files) - see the table below
for exactly which ones.

## What "audited" means

A module is exported only when **every one of its files** carries a fresh stamp on all five
dimensions, and so does **every module it reaches**:

| Dimension | What passed |
| :-- | :-- |
| `style` | the strict CFW style guide (const placement, naming, structure) |
| `memsec` | a memory-safety and security review of every changed hunk |
| `test` | its `tests/<module>/` suite built and green, with the whole tree relinked |
| `design` | an API design loop run to convergence (no open Critical/High/Mid items) |
| `platform` | built and green on Linux (gcc 14, ASan/UBSan) as well as Windows |

The closure rule is mechanical: `tools/export_cfw.py` in the upstream tree computes what each
module's sources *and* suite include, transitively, and refuses to export a module if anything
in that set is unstamped. There are no waivers. A module with sources must also carry a suite
you can run here; header-only modules are covered by their includers' suites.

Each module's own contract - which inputs are refused as no-ops versus which are programming
errors that abort in checked builds - is documented in its header's `Error Handling` section.

## Modules

| Module | Files | Suite | Role |
| :-- | :-- | :-- | :-- |
| `arena` | 4 .c / 4 .h | with its dependencies | Canonical arena interface for the C Libraries Framework |
| `chrono` | 1 .c / 1 .h | tests/chrono/ |  |
| `console` | 1 .c / 1 .h | tests/console/ | Cross-platform terminal library for x64 systems |
| `error` | 1 .c / 1 .h | with its dependencies | Centralized error checking utilities for the C Libraries Framework |
| `log` | 1 .c / 1 .h | tests/log/ | Centralized logging and debug tracing for the C Libraries Framework |
| `math` | 36 .c / 39 .h | tests/math/ | 2D axis-aligned bounding box operations for the CFW math module |
| `memory` | 1 .c / 1 .h | with its dependencies | Canonical memory management utilities for the C Libraries Framework |
| `platform/windows` | 0 .c / 1 .h | tests/platform/windows/ | Canonical entry point for the Windows system headers |
| `result` | 0 .c / 1 .h | header-only | Packed status/error code type for CFW |
| `test` | 1 .c / 1 .h | tests/test/ | Small test runner for the C Libraries Framework |
| `thread` | 1 .c / 1 .h | tests/thread/ | Cross-platform thread and synchronization primitives for the C Libraries Framework |
| `tracelog` | 1 .c / 1 .h | tests/tracelog/ | Manual call-stack tracing for the C Libraries Framework |
| `tuple` | 3 .c / 4 .h | tests/tuple/ | Tuple type (bool, USize) for the C Libraries Framework |
| `types` | 0 .c / 1 .h | header-only | Standard type definitions and platform macros for cross-platform C |

## Building

Requires a **C23 compiler** (gcc 14+ or clang 18+; MSVC is not supported) and, as system packages:

```sh
# Debian / Ubuntu
sudo apt install libcglm-dev

# MSYS2 (UCRT64)
pacman -S mingw-w64-ucrt-x86_64-cglm
```

```sh
make            # libcfw.a
make test       # every tests/**/test_*.c, built against libcfw.a and run
make test-oom   # Linux: the allocation-failure harness (GNU ld --wrap)
```

`tests/platform/windows/` is built and run on Windows only - the module it covers compiles to
nothing elsewhere. Every other suite runs on both legs of CI.

`make CC=clang`, `CFLAGS=...` and `CSTD=-std=c2x` (for gcc 13 / clang 16-17) work as usual.
A `CMakeLists.txt` mirrors the same build (`cmake -B build && cmake --build build && ctest --test-dir build`).

**Why 0.9.6 exactly.** `math` was written against a post-0.9.6 cglm snapshot that exports eight compiled entry points the released library does not. `include/math/cglm_compat.h` carries those eight bodies so the facade links against the cglm your package manager ships, and every other cglm symbol the module uses is pinned against 0.9.6's export list upstream. A newer cglm is not tested and a mismatch will show up as duplicate or missing symbols at link time, not silently.

Feature macros (`ARENA_IMPLEMENTATION`, `ERROR_CHECK_ENABLED`, `LOG_THREAD_IMPLEMENTATION`,
`MEMORY_HOOKS_IMPLEMENTATION`, `MEMORY_NON_DANGLING_POINTER`, `TRACELOG_ENABLED`) select each
module's implementation and are set in the Makefile exactly as the modules were audited with.
`ERROR_CHECK_ENABLED` is the checked build: programming errors abort with a logged location.
Without it those checks compile out; the value-dependent refusals stay in every build.

## Using a module

```c
#include <container/string/string.h>   /* when exported */
#include <math/vec3.h>
#include <arena/arena.h>

Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
Vec3  up    = vec3_make(0.0f, 1.0f, 0.0f);
/* ... */
arena_uninit(&arena, ARENA_TYPE_LINEAR);
```

Every header begins with a documented brief: the module's features, its error-handling
contract, thread-safety notes and dependencies. Read that before the function list.

## Contributing

This tree is a **derived export**. The upstream monorepo is the source of truth and carries
the audit machinery (the reviewers, the stamps, the design loop); GitHub contributions are
brought back into it by hand, re-audited there, and re-exported. Open an issue or a pull
request here as usual - a patch is welcome even though it will not be merged into this tree
directly - and expect the fix to land in the next export rather than as a commit on top of
yours.

## License

MIT - see [LICENSE](LICENSE).
