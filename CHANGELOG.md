# Changelog

All notable changes to the public CFW export. Versioning follows [SemVer 2.0.0](https://semver.org/).

A module arrives in its own commit, the moment it has cleared all five audit dimensions
upstream; a version is tagged when a family is complete, so entries accumulate here first.

## Unreleased

- `encoding/hex`
- `encoding/base64`

## 0.2.0 - release

The container wave. Four containers join the foundation, each through the same
five-dimension gate as the first export, and the tools that keep their families
honest ship with them.

- `container/arrayList` - thirteen typed dynamic arrays, one per element type,
  with al_u64 as the canonical instantiation; `tools/al_divergence.py` measures
  every other file's divergence from it and `make check` runs it here.
- `container/map` - nine string-keyed maps in three value shapes; four
  hand-maintained pairs and five scalars generated from the u64 pair by
  `tools/map_generate.py`, with `tools/map_divergence.py` proving they match.
- `container/hashset` - a counting string set (borrow or copy the key, sized
  slice forms, a cursor walk) whose every borrow declines instead of aborting.
- `container/slotmap` - generational 32-bit handles over a caller-owned pool,
  with a stated walk-under-removal guarantee and an ABA bound of exactly 65535.
- `env` and `bits` - the two modules that landed between the tags: a dotenv
  loader and the single-word and bit-array primitives behind al_bool.
- Build: `make check` runs the family drift gates (python3) and `make test`
  depends on it; `make test-unchecked` builds every `test_unchecked.c` without
  ERROR_CHECK_ENABLED against its own archive; the Linux CI leg runs in a
  debian:13 container, which packages the cglm the math module pins.
- Fixed: `math`'s `mat4_inv_fast` pins allow the approximation the instruction
  promises rather than one CPU model's exact result; the test loops name the
  suite that fails and its exit code.

## 0.1.0 - first export

The foundation and `math`, audited on all five dimensions with their whole include closure:
`allocator`, `arena`, `char`, `chrono`, `console`, `container/str`, `container/string`, `datetime`, `dir`, `error`, `file`, `log`, `math`, `memory`, `platform/windows`, `process`, `regex`, `result`, `test`, `thread`, `tracelog`, `tuple`, `types`.

- `math` 0.3.1: value-typed facade over cglm 0.9.6 with `cglm_compat.h` supplying the eight
  post-release entry points; refusals over aborts for data-shaped inputs.
- `memory` / `allocator` / `arena`: the allocation layer, with the Linux allocation-failure
  harness (`make test-oom`) proving every documented decline path against a real failed calloc.
- `log` / `tracelog` / `error` / `thread` / `console` / `chrono` / `tuple` / `types` / `result`.
