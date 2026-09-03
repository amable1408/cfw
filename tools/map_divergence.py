#!/usr/bin/env python3
"""map_divergence.py - drift gate for the generated members of CFW's map family.

`include/container/map/` holds nine hand-written instantiations that converged onto one API
on 2026-08-25. They fall into three value SHAPES, and only one of those is generated:

    pointer   map_char_char                      hand-written, canonical
    scalar    map_char_u64                       hand-written, canonical
              map_char_{u8,u16,u32,f32,f64}      GENERATED from map_char_u64
    struct    map_char_al_char, map_char_string  hand-written (different ownership models)

The five generated files are what this gate protects. Their review stamps were granted on a
DEMONSTRATED equivalence to map_char_u64 - not on the claim "a generator made them" - and the
memory/security lane was explicit that the weaker framing would be a stamp on a claim about a
generator rather than on the artifact. That demonstration has to keep being true, and nothing
else in the tree checks it: a hand-edit to one generated file leaves every stamp FRESH and every
suite green, because map_char_u64's suite is the only one that runs.

Method:
  1. normalize each generated file's type tokens back to the anchor's (u64/U64/AL_U64),
  2. diff against the anchor, ignoring only the trailing @audited marker,
  3. re-derive the counts each header STATES about itself and check they still hold.

Step 3 is not redundant with step 2. Step 2 proves the five agree with the anchor; step 3 proves
the four CANONICAL headers still agree with their own prose, which is where two real defects
lived this campaign. All four enumerate their abort sites exactly rather than illustratively -
that was the campaign's standard, and it is only affordable because this re-derives them.

Usage:
    python tools/map_divergence.py            # gate: report and exit 1 on any drift
    python tools/map_divergence.py --verbose  # also print the differing lines

Exit code: 0 = every generated file matches its anchor and every header's counts hold,
           1 = drift, 2 = usage or a missing file.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPS = os.path.join(ROOT, "include", "container", "map")

ANCHOR = ("u64", "U64")

GENERATED = [("u8", "U8"), ("u16", "U16"), ("u32", "U32"), ("f32", "F32"), ("f64", "F64")]

# What each canonical header claims about itself, in words, beside what its own source
# actually does. A header whose numbers are decorative cannot be trusted on the safety claims
# sitting beside them, which is why these are checked rather than assumed - the 42/34 pair was
# wrong by hand before it was measured.
#
# `null` counts PUBLIC error_check_null sites only; the private helpers carry their own and
# differ per file (10, 10, 13, 12), which is exactly why the count has to be derived by
# walking function heads rather than by subtracting a hardcoded number.
CANONICAL = {
    "map_char_char": {"null": 42, "with_null": 30, "functions": 34, "index": 3, "capacity": 5,
                      "word": "forty-two"},
    "map_char_u64": {"null": 42, "with_null": 30, "functions": 34, "index": 3, "capacity": 5,
                     "word": "forty-two"},
    "map_char_al_char": {"null": 46, "with_null": 30, "functions": 34, "index": 3, "capacity": 5,
                         "word": "forty-six"},
    "map_char_string": {"null": 46, "with_null": 30, "functions": 34, "index": 3, "capacity": 5,
                        "word": "forty-six"},
}

# Phrases every canonical header must still carry. The count words are checked per file from
# CANONICAL["word"]; these are the ones that are the same in all four.
STATED = ["thirty-four functions", "at five sites"]

# A function-definition head. The `static` group and a leading underscore are what separate a
# private helper from the public surface the headers count.
SIGNATURE = re.compile(r"^(static\s+)?[A-Za-z_][A-Za-z_0-9]*\s*\*{0,2}\s*(_?[a-z][a-z0-9_]*)\("
                       r"[^;]*\)\s*\{", re.M)


def _read(path):
    with open(path, "r", encoding="utf-8", newline="") as handle:
        return handle.read()


def _strip_marker(text):
    return re.sub(r"\n//\s*@audited[^\n]*$", "", text)


def _normalize(text, lower, upper):
    """Rewrite one instantiation's type tokens to the anchor's, longest first.

    Order matters: AL_U8 must be rewritten before a bare U8 would match inside it, and the
    include guard carries the upper form too.
    """
    anchor_lower, anchor_upper = ANCHOR

    text = text.replace("map_char_%s" % lower, "map_char_%s" % anchor_lower)
    text = text.replace("MAP_CHAR_%s" % upper, "MAP_CHAR_%s" % anchor_upper)
    text = text.replace("Map_Char_%s" % upper, "Map_Char_%s" % anchor_upper)
    text = text.replace("AL_%s" % upper, "AL_%s" % anchor_upper)
    text = text.replace("al_%s" % lower, "al_%s" % anchor_lower)
    text = text.replace(upper, anchor_upper)

    return text


def _counts(source_text):
    """Re-derive what a canonical header states about itself, from its source.

    Splitting the translation unit at every function head is what makes the public/private
    split visible: the headers count the PUBLIC surface, and the private helpers' share of
    error_check_null differs from file to file.
    """
    heads = list(SIGNATURE.finditer(source_text))
    counts = {
        "null": 0,
        "with_null": 0,
        "functions": 0,
        "capacity": source_text.count('error_check_non_value_uint(LOG_METADATA, "capacity"'),
        "index": source_text.count("error_check_out_of_bound_uint"),
    }

    for position, head in enumerate(heads):
        end = heads[position + 1].start() if position + 1 < len(heads) else len(source_text)

        if head.group(1) is not None or head.group(2).startswith("_"):
            continue

        # TEXTUAL count: a COMMENT naming error_check_null inside a public function would
        # inflate this and fail the gate - a loud false positive, accepted. The .c files
        # currently never name the primitive in comments; that is the invariant this relies
        # on, and a confusing gate failure after a doc edit is the symptom of breaking it.
        nulls = source_text[head.start():end].count("error_check_null")
        counts["functions"] = counts["functions"] + 1
        counts["null"] = counts["null"] + nulls

        if nulls:
            counts["with_null"] = counts["with_null"] + 1

    return counts


def _declarations(header_text, name):
    """Count public declarations, to pair against definitions."""
    return len(re.findall(r"^[A-Za-z_][A-Za-z_0-9]*\s*\*? %s_[a-z]" % name, header_text, re.M))


def main():
    verbose = "--verbose" in sys.argv[1:]

    for argument in sys.argv[1:]:
        if argument != "--verbose":
            print("usage: python tools/map_divergence.py [--verbose]")

            return 2

    anchor_lower, anchor_upper = ANCHOR
    anchor_source_path = os.path.join(MAPS, "map_char_%s.c" % anchor_lower)
    anchor_header_path = os.path.join(MAPS, "map_char_%s.h" % anchor_lower)

    for path in (anchor_source_path, anchor_header_path):
        if not os.path.exists(path):
            print("missing anchor: %s" % path)

            return 2

    anchor_source = _strip_marker(_read(anchor_source_path))
    anchor_header = _strip_marker(_read(anchor_header_path))

    failed = []

    # ---------------------------------------------------------------- step 1 and 2
    print("generated-vs-anchor (anchor: map_char_%s)" % anchor_lower)

    for lower, upper in GENERATED:
        for suffix, anchor_text in ((".c", anchor_source), (".h", anchor_header)):
            path = os.path.join(MAPS, "map_char_%s%s" % (lower, suffix))

            if not os.path.exists(path):
                print("  map_char_%-4s%s  MISSING" % (lower, suffix))
                failed.append("map_char_%s%s" % (lower, suffix))

                continue

            normalized = _normalize(_strip_marker(_read(path)), lower, upper)
            differing = [(n, a, b) for n, (a, b)
                         in enumerate(zip(normalized.split("\n"), anchor_text.split("\n")), 1)
                         if a != b]
            length_gap = len(normalized.split("\n")) - len(anchor_text.split("\n"))

            if differing or length_gap != 0:
                print("  map_char_%-4s%s  DRIFT: %d differing line(s), %+d line(s)"
                      % (lower, suffix, len(differing), length_gap))
                failed.append("map_char_%s%s" % (lower, suffix))

                if verbose:
                    for number, actual, expected in differing[:20]:
                        print("      %d:\n        got:      %s\n        anchor:   %s"
                              % (number, actual.strip(), expected.strip()))
            else:
                print("  map_char_%-4s%s  identical" % (lower, suffix))

    # ---------------------------------------------------------------- step 3
    print("\ncanonical-vs-its-own-prose")

    for name in sorted(CANONICAL):
        stated = CANONICAL[name]
        source_path = os.path.join(MAPS, "%s.c" % name)
        header_path = os.path.join(MAPS, "%s.h" % name)

        if not os.path.exists(source_path) or not os.path.exists(header_path):
            print("  %-16s MISSING" % name)
            failed.append("%s missing" % name)

            continue

        source = _strip_marker(_read(source_path))
        header = _strip_marker(_read(header_path))
        counts = _counts(source)
        declarations = _declarations(header, name)
        problems = []

        if declarations != counts["functions"]:
            problems.append("%d declarations != %d definitions" % (declarations, counts["functions"]))

        for key in ("null", "with_null", "functions", "index", "capacity"):
            if counts[key] != stated[key]:
                problems.append("%s is %d, header states %d" % (key, counts[key], stated[key]))

        # The number has to be in the PROSE too, not only in this table - a table that agrees
        # with the source while the header says something else is the defect being gated.
        if "%s public sites" % stated["word"] not in header:
            problems.append("header does not say %r public sites" % stated["word"])

        for phrase in STATED:
            if phrase not in header:
                problems.append("header no longer says %r" % phrase)

        if problems:
            print("  %-16s DRIFT" % name)

            for problem in problems:
                print("      %s" % problem)

            failed.append("%s prose" % name)
        else:
            print("  %-16s %d declarations / %d definitions, %d public null sites across %d "
                  "functions, %d index, %d capacity"
                  % (name, declarations, counts["functions"], counts["null"], counts["with_null"],
                     counts["index"], counts["capacity"]))

    if failed:
        print("\nFAIL: %s" % ", ".join(sorted(set(failed))))

        # The two failure classes want opposite repairs, and printing both every time is how a
        # gate teaches the wrong one. Drift in a GENERATED file is never fixed in that file.
        if any(not item.endswith("prose") for item in failed):
            print("A generated file must be regenerated, never hand-edited - fix map_char_%s,"
                  % anchor_lower)
            print("then run `python tools/map_generate.py`, or the family diverges the way it")
            print("did before 2026-08-25.")

        if any(item.endswith("prose") for item in failed):
            print("A canonical header states its abort counts exactly. Recount with the numbers")
            print("above, update BOTH the header's prose and this file's CANONICAL table, and")
            print("say in the commit message which edit moved them.")

        return 1

    print("\nOK: five generated instantiations match their anchor, and all four canonical")
    print("headers match their own prose.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
# @audited cfw style=5aa078e5a083 memsec=5aa078e5a083 test=5aa078e5a083 design=5aa078e5a083
