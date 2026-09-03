#!/usr/bin/env python3
"""map_generate.py - regenerate the five generated members of CFW's map family.

`include/container/map/` holds nine hand-written instantiations. Five of them are not
hand-written at all: map_char_{u8,u16,u32,f32,f64} are map_char_u64 with its type tokens
substituted, and `tools/map_divergence.py` is the gate that keeps that true.

That gate's failure message has always said "fix map_char_u64 and re-run the generator",
which was a description of a procedure rather than of a program - the substitution was done
by hand. This is the program. It matters because the procedure is the thing being relied on:
the five carry no test suite of their own, and their review stamps were granted on a
DEMONSTRATED equivalence to the anchor rather than on the claim that a generator made them.

The substitution is exactly the inverse of map_divergence.py's `_normalize`, which is what
makes the pair verifiable: generate here, then run the gate and watch it report `identical`.

Usage:
    python tools/map_generate.py            # rewrite the five pairs from map_char_u64
    python tools/map_generate.py --check    # report what would change, write nothing

Each target's trailing @audited marker line is carried over verbatim, so a regeneration that
changes the body leaves the stamps reading STALE - which is the correct signal, not a defect.

Exit code: 0 = written (or, under --check, nothing would change),
           1 = under --check, at least one file would change,
           2 = usage or a missing anchor.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPS = os.path.join(ROOT, "include", "container", "map")

ANCHOR = ("u64", "U64")

GENERATED = [("u8", "U8"), ("u16", "U16"), ("u32", "U32"), ("f32", "F32"), ("f64", "F64")]

MARKER = re.compile(r"\n//\s*@audited[^\n]*$")


def _read(path):
    with open(path, "r", encoding="utf-8", newline="") as handle:
        return handle.read()


def _write(path, text):
    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)


def _substitute(text, lower, upper):
    """Rewrite the anchor's type tokens to one instantiation's, longest first.

    Order matters and is the inverse of map_divergence.py's `_normalize`: every compound
    spelling that CONTAINS the bare token has to be rewritten before the bare token is, or
    `AL_U64` would be reached twice and come out as `AL_U8` only by luck of ordering.
    """
    anchor_lower, anchor_upper = ANCHOR

    text = text.replace("map_char_%s" % anchor_lower, "map_char_%s" % lower)
    text = text.replace("MAP_CHAR_%s" % anchor_upper, "MAP_CHAR_%s" % upper)
    text = text.replace("Map_Char_%s" % anchor_upper, "Map_Char_%s" % upper)
    text = text.replace("AL_%s" % anchor_upper, "AL_%s" % upper)
    text = text.replace("al_%s" % anchor_lower, "al_%s" % lower)
    text = text.replace(anchor_upper, upper)

    return text


def main():
    check = "--check" in sys.argv[1:]

    for argument in sys.argv[1:]:
        if argument != "--check":
            print("usage: python tools/map_generate.py [--check]")

            return 2

    anchor_lower, anchor_upper = ANCHOR
    sources = {}

    for suffix in (".c", ".h"):
        path = os.path.join(MAPS, "map_char_%s%s" % (anchor_lower, suffix))

        if not os.path.exists(path):
            print("missing anchor: %s" % path)

            return 2

        sources[suffix] = _read(path)

    changed = []

    for lower, upper in GENERATED:
        for suffix in (".c", ".h"):
            path = os.path.join(MAPS, "map_char_%s%s" % (lower, suffix))
            body = _substitute(MARKER.sub("", sources[suffix]), lower, upper)

            # The target's own marker is kept: this script regenerates CODE, and the review
            # stamps are the audit hook's to grant. A changed body makes them read STALE.
            if os.path.exists(path):
                existing = _read(path)
                marker = MARKER.search(existing)
                text = body + (marker.group(0) if marker is not None else "")

                if text == existing:
                    print("  map_char_%-4s%s  unchanged" % (lower, suffix))

                    continue
            else:
                text = body

            changed.append("map_char_%s%s" % (lower, suffix))

            if check:
                print("  map_char_%-4s%s  WOULD CHANGE" % (lower, suffix))
            else:
                _write(path, text)
                print("  map_char_%-4s%s  written" % (lower, suffix))

    if check and changed:
        print("\n%d file(s) differ from map_char_%s. Run without --check to regenerate,"
              % (len(changed), anchor_lower))
        print("then `python tools/map_divergence.py` to confirm the substitution inverted.")

        return 1

    if not changed:
        print("\nNothing to do: the five already match map_char_%s." % anchor_lower)
    else:
        print("\nRegenerated %d file(s). Run `python tools/map_divergence.py` to confirm, and"
              % len(changed))
        print("rebuild - a header change is invisible to the makefiles' dependency tracking.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
# @audited cfw design=b4b05e7897a7
