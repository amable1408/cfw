# CFW

[![CI](https://github.com/amable1408/cfw/actions/workflows/ci.yml/badge.svg)](https://github.com/amable1408/cfw/actions/workflows/ci.yml)

A modular **C23 framework**: self-contained modules, one translation unit each, a paired header
per module, and a test suite per module. This repository is the **audited export** of CFW - it
holds only the modules that have cleared every gate below, together with everything they include.

**26 modules** (157 files) at HEAD; the last tagged release is **0.1.0**. A
module lands in its own commit as soon as it clears the gate; a version is tagged when a family
is complete. See the table below for exactly which modules are here.

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
| `allocator` | 1 .c / 1 .h | tests/allocator/ | Canonical allocator interface for the C Libraries Framework |
| `arena` | 4 .c / 4 .h | tests/arena/ | Canonical arena interface for the C Libraries Framework |
| `bits` | 1 .c / 1 .h | tests/bits/ | Bit manipulation utilities for the C Libraries Framework |
| `char` | 1 .c / 1 .h | tests/char/ | Null-terminated char-buffer string utilities for the CFW framework |
| `chrono` | 1 .c / 1 .h | tests/chrono/ |  |
| `console` | 1 .c / 1 .h | tests/console/ | Cross-platform terminal library for x64 systems |
| `container/arrayList` | 13 .c / 13 .h | tests/container/arrayList/ | Dynamic array list of U64 for the C Libraries Framework |
| `container/str` | 1 .c / 1 .h | tests/container/str/ | Canonical string buffer (Str) utilities for the C Libraries Framework |
| `container/string` | 1 .c / 1 .h | tests/container/string/ | Canonical String Object for the C Libraries Framework |
| `datetime` | 1 .c / 1 .h | tests/datetime/ | Date and time utilities for the C Libraries Framework |
| `dir` | 1 .c / 1 .h | tests/dir/ | Directory operations for the C Libraries Framework |
| `env` | 1 .c / 1 .h | tests/env/ | Environment variable management (dotenv-style) for the C Libraries Framework |
| `error` | 1 .c / 1 .h | tests/error/ | Centralized error checking utilities for the C Libraries Framework |
| `file` | 1 .c / 1 .h | tests/file/ | Canonical file I/O interface for reading, writing, and managing files |
| `log` | 1 .c / 1 .h | tests/log/ | Centralized logging and debug tracing for the C Libraries Framework |
| `math` | 36 .c / 39 .h | tests/math/ | 2D axis-aligned bounding box operations for the CFW math module |
| `memory` | 1 .c / 1 .h | tests/memory/ | Canonical memory management utilities for the C Libraries Framework |
| `platform/windows` | 0 .c / 1 .h | tests/platform/windows/ | Canonical entry point for the Windows system headers |
| `process` | 1 .c / 1 .h | tests/process/ | Spawn a child program with piped stdin/stdout and capture what it writes |
| `regex` | 1 .c / 1 .h | tests/regex/ | PCRE2-based regular expression wrapper for the C Libraries Framework |
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
sudo apt install libcglm-dev libpcre2-dev

# MSYS2 (UCRT64)
pacman -S mingw-w64-ucrt-x86_64-cglm mingw-w64-ucrt-x86_64-pcre2
```

```sh
make            # libcfw.a
make test       # every tests/**/test_*.c, built against libcfw.a and run
make test-oom   # Linux: the allocation-failure harness (GNU ld --wrap)
make test-unchecked   # every tests/**/test_unchecked.c, built WITHOUT ERROR_CHECK_ENABLED
                      # against its own archive: the inert-fallback half of a contract
make check      # the family drift gates under tools/ (python3); `make test` runs it first
```

`tests/platform/windows/` is built and run on Windows only - the module it covers compiles to
nothing elsewhere. Every other suite runs on both legs of CI.

`make CC=clang`, `CFLAGS=...` and `CSTD=-std=c2x` (for gcc 13 / clang 16-17) work as usual.
A `CMakeLists.txt` mirrors the same build (`cmake -B build && cmake --build build && ctest --test-dir build`).

**cglm must be 0.9.6.** Debian 13 and MSYS2 package exactly that, and CI runs on Debian 13 for the same reason. Ubuntu 24.04's 0.9.2 is too old - it does not declare the `aabb2d` entry points `math` calls, so the build fails at compile time rather than silently. Getting the right version is your package manager's problem, not this Makefile's; note only that a cglm you build yourself may enable a SIMD path whose `rcpps` approximation costs `mat4_inv_fast` about 2^-12 of precision, which the suite's exactness assertions will catch.

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
