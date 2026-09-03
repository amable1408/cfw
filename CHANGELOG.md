# Changelog

All notable changes to the public CFW export. Versioning follows [SemVer 2.0.0](https://semver.org/).

A module arrives in its own commit, the moment it has cleared all five audit dimensions
upstream; a version is tagged when a family is complete, so entries accumulate here first.

## Unreleased

- `env`
- sync: env
- `bits`
- sync: math
- `container/arrayList`
- `container/map`

## 0.1.0 - first export

The foundation and `math`, audited on all five dimensions with their whole include closure:
`allocator`, `arena`, `char`, `chrono`, `console`, `container/str`, `container/string`, `datetime`, `dir`, `error`, `file`, `log`, `math`, `memory`, `platform/windows`, `process`, `regex`, `result`, `test`, `thread`, `tracelog`, `tuple`, `types`.

- `math` 0.3.1: value-typed facade over cglm 0.9.6 with `cglm_compat.h` supplying the eight
  post-release entry points; refusals over aborts for data-shaped inputs.
- `memory` / `allocator` / `arena`: the allocation layer, with the Linux allocation-failure
  harness (`make test-oom`) proving every documented decline path against a real failed calloc.
- `log` / `tracelog` / `error` / `thread` / `console` / `chrono` / `tuple` / `types` / `result`.
