#!/usr/bin/env python3
"""al_divergence.py - drift gate for CFW's hand-cloned al_* generic family.

`include/container/arrayList/` holds 13 hand-written typed instantiations (plus
al_multipart, a 14th that lives beside its consumer in http/service/multipart) that
are supposed to be the same code modulo element type. Nothing in the tree
checked that, and a Block F audit found five files still carrying bugs an
earlier sweep had recorded as "fixed family-wide". This script makes that drift
a one-command check instead of an audit.

(al_ipquery_result left the family 2026-09-03, relocated to
include/service/ipquery/ - it depended on a service header, which put the
whole generic-container closure behind the entire web stack in the public
export. Its own drift notes moved with it, out of this file.)

Method (the audit's, scripted):
  1. every al_*.c / al_*.h pair is discovered from the directory,
  2. each file's identity tokens are derived from the file itself - the
     function prefix (`al_u64`), the struct typedef (`AL_U64`), the element
     type (from the `*_alloc_init_3` signature) and the growth macro - and
     rewritten to one placeholder, so an instantiation is normalized toward the
     canonical without a per-type substitution table,
  3. comments and blank lines are stripped, so prose differences are not drift,
  4. what remains is diffed against the canonical and the differing lines are
     counted.

A file that is not in the ALLOWLIST must be a zero-diff clone. An allowlisted
file gets a recorded budget - the measured count when the entry was written -
and fails if it grows past it. Shrinking below the budget is reported as
RATCHET: lower the number in the same commit as the fix.

Usage:
    python tools/al_divergence.py                 # gate: table + unallowed diffs
    python tools/al_divergence.py --verbose       # also print allowed diffs
    python tools/al_divergence.py --canonical al_void

Exit code: 0 = every instantiation is within its budget, 1 = drift, 2 = usage.

Known limitation - read the three keyword-element counts with care. When the
element type is a C keyword (al_bool `bool`, al_char `char`, al_void `void`) the
type token is NOT substituted, because rewriting `void` would also rewrite
`(void*)` casts and `void` returns. Those three therefore measure well above
their real divergence, and al_void is the extreme case: of its 234 .c lines only
about 24 are real (integer growth, `i += 1`, `nullptr` for the float literals -
the R10/R31 modernizations the template should adopt), and all 24 of its header
lines are artifact. al_void is close to a clone, not the family's worst drifter.
Measured shares: al_void ~210 of 234 artifact, al_bool ~198 of 351, al_char ~80
of 161. Headers are unaffected for al_bool and al_char.
"""
import argparse
import difflib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAMILY_DIR = os.path.join(ROOT, "include", "container", "arrayList")

# Clones that live OUTSIDE the family directory, beside their only consumer,
# because a generic container must not depend on a service header. They are
# still clones of the canonical and the gate measures them like any other;
# a relocation must never silently drop a file out of the scan.
EXTRA = {
    "al_multipart": os.path.join(ROOT, "include", "http", "service", "multipart"),
}

# The instantiation every other one is normalized toward. al_u64 is the audit's
# choice: it is the tested shape and the one al_u16 / al_i64 are byte-equivalent
# clones of. Overridable with --canonical.
CANONICAL = "al_u64"

# Every identity token collapses to this, so `AL_U64` and `AL_Str` compare equal.
PLACEHOLDER = "@T@"

# Element types that are C keywords: substituting them would rewrite unrelated
# code, so they are skipped. See the module docstring.
KEYWORD_ELEMENTS = ("bool", "char", "double", "float", "int", "long", "short", "void")

# Deliberate divergences: instantiations that CANNOT be zero-diff clones because
# their element contract genuinely differs. Each entry says WHY - a blanket
# "this file is different" exemption is exactly the hiding place this gate
# exists to remove - and carries the budget it may not exceed. Grow past the
# budget and the gate fails; shrink below it and the gate says RATCHET, meaning
# lower the number in the same commit as the fix.
#
# Budgets re-measured 2026-09-03 after the round-2 fix pass (init_3 bound,
# delete-via-uninit and const sources converged; the normalizer now folds
# whitespace and keyword pointer spellings, so an alignment-only or
# `void **` line no longer counts). They are a snapshot of today's code, NOT
# a certificate. Every ruled-permanent divergence lives HERE: a gate that
# fails forever for a file that will never converge is a gate nobody runs.
ALLOWLIST = {
    # Owns its elements: add copies through str_init, uninit frees per element,
    # and remove releases BEFORE the shift (the aliasing fix al_str.c documents
    # in a comment). None of that exists in a value clone.
    "al_str": {"reason": "owns its Str elements (copy/uninit semantics); add refuses an own element",
               "c_max": 50, "h_max": 16},

    # The same ownership contract over String instead of Str, so it is really
    # al_str's twin - check it with `--canonical al_str`, not against al_u64.
    "al_string": {"reason": "owns its String elements (copy/uninit semantics); add refuses an own element",
                  "c_max": 50, "h_max": 16},

    # Nested lists: the element is itself an AL_Char, so ownership is recursive
    # and clear uninits each element (R18). init_3/alloc_init_3 carry a
    # refused-borrow guard the canonical has no reason to, since only the
    # element-OWNING instantiations copy element-by-element.
    "al_al_char": {"reason": "holds nested AL_Char lists (recursive ownership); add refuses an own element",
                   "c_max": 109, "h_max": 25},

    # Deliberate bit-packed rewrite: there is no element array at all, so add,
    # at and clear are word/bit arithmetic with by-value bool accessors rather
    # than a value copy. Part of the .c count is the unsubstituted `bool`
    # element token (see the docstring); the rest is the real rewrite, plus
    # the refused-arena and word-count guards in reserve and both constructors,
    # the integer _al_bool_words ceiling, and at()'s second capacity bound
    # (this file exposes the unchecked set_size). Growth from an unguarded
    # borrow learning to refuse is convergence toward correct - do not "fix"
    # it back.
    "al_bool": {"reason": "deliberate bit-packed rewrite (no element array)",
                "c_max": 308, "h_max": 28},

    # Owns its char* element buffers: clear and remove release them through the
    # list's allocator, which a value clone never does (al_u64_clear releases
    # nothing - a U64 has nothing to release). Same structural class as al_str
    # and al_string; init_3 copies element by element for the same reason.
    "al_char": {"reason": "owns its char* element buffers (release on clear/remove)",
                "c_max": 52, "h_max": 18},

    # Typed zero literals: F32 slots are cleared with 0.0f (F64 with 0.0) where
    # the canonical writes the integer 0 - correct typing, ruled permanent on
    # 2026-08-23. Four lines each, and exactly four: a fifth is drift.
    "al_f32": {"reason": "F32 slots cleared with 0.0f where the canonical writes 0 - correct typing",
               "c_max": 4, "h_max": 0},
    "al_f64": {"reason": "F64 slots cleared with 0.0 where the canonical writes 0 - correct typing",
               "c_max": 4, "h_max": 0},

    # Exposes set_size (the silent clamp to capacity an external writer that
    # fills get_data() relies on, R17 KEEP) and therefore a second capacity
    # bound in at(), since a lying size must not read out of bounds.
    "al_u8": {"reason": "set_size (a silent clamp to capacity) and the dual at() bound it needs",
              "c_max": 7, "h_max": 1},

    # `void*` elements: at() returns the SLOT (`void**`), a refused growth is
    # clamped in integer arithmetic, and the remaining lines are the `void`
    # keyword in spellings the normalizer does not fold (bare casts). Ruled
    # permanent with R10/R31 closed.
    "al_void": {"reason": "void* elements: at() returns the slot, integer growth clamp",
                "c_max": 36, "h_max": 10},

    # Stores HTTP_Service_Multipart_Node BY VALUE and owns none of its pointers:
    # add takes the node by address (a five-field struct) and refuses an
    # own-list element, and the typedef the list stores sits in this header
    # pair beside its only consumer (EXTRA above).
    "al_multipart": {"reason": "struct nodes by value, add by pointer with own-element refusal, no ownership",
                     "c_max": 18, "h_max": 17},
}

# Instantiations that are SUPPOSED to converge to the canonical and have not
# yet. They fail the gate; this map only says WHAT still differs, so a genuinely
# new divergence is distinguishable from a known one at a glance. Empty since
# 2026-09-03: every ruled-permanent divergence moved to ALLOWLIST with a budget,
# so an unlisted, non-zero instantiation is a NEW divergence, never a known one.
#
# Keep any note added here true. A note citing an item that has since been
# fixed is worse than no note, because it reads as "known and accepted".
BACKLOG = {
}


#== MARK: - Normalization ==#

def strip_comments(text):
    """Remove C comments, leaving string and character literals untouched."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"' or ch == "'":
            quote = ch
            out.append(ch)
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i])
                    out.append(text[i + 1])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def tokens_of(name, source, header):
    """Identity tokens of one instantiation, derived from its own text.

    Returns a dict with the function prefix, the struct typedef, the element
    type, the growth macro and the upper-case name token, or None when the file
    is not shaped like a member of the family.
    """
    prefix = name

    type_match = re.search(r"^\}\s*(AL_[A-Za-z0-9_]+)\s*;", header, re.M)
    if type_match is None:
        return None

    element_match = re.search(
        re.escape(prefix) + r"_(?:alloc_)?init_3\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:const\s*)?\*", source)
    if element_match is None:
        return None

    growth_match = re.search(r"#define\s+(_AL_[A-Z0-9_]+_GROWTH_FACTOR)", source)

    return {
        "prefix": prefix,
        "type": type_match.group(1),
        "element": element_match.group(1),
        "growth": growth_match.group(1) if growth_match else None,
        "upper": prefix[len("al_"):].upper(),
    }


def _sub_token(text, token, replacement):
    """Replace a whole identifier, never a piece of a longer one."""
    return re.sub(r"(?<![A-Za-z0-9_])" + re.escape(token) + r"(?![A-Za-z0-9_])",
                  replacement, text)


def _sub_part(text, token, replacement):
    """Replace an underscore-delimited part of an identifier.

    The token may sit at the start (`al_u64` in `al_u64_add`), in the middle
    (`U64` in `CONTAINER_ARRAYLINKEDLIST_U64_H`) or behind a leading underscore
    (`al_u64` in the file-local `_al_u64_init`) - all three are the same
    identity token wearing different neighbours, and missing any of them makes
    a byte-equivalent clone measure as drift.
    """
    return re.sub(r"(?<![A-Za-z0-9])" + re.escape(token) + r"(?![A-Za-z0-9])",
                  replacement, text)


def normalize(text, tokens):
    """Rewrite one instantiation's identity tokens to the placeholder, then drop
    comments and blank lines.

    Longest tokens go first so `AL_AL_Char` is not eaten by its own element type
    `AL_Char`, and the growth macro goes before the upper-case name token so a
    drifted macro name (`_AL_STRING_` in al_str) does not leave a fragment.
    """
    text = strip_comments(text)

    if tokens["growth"]:
        text = _sub_token(text, tokens["growth"], "_AL_" + PLACEHOLDER + "_GROWTH_FACTOR")
    text = _sub_part(text, tokens["type"], "AL_" + PLACEHOLDER)
    text = _sub_part(text, tokens["prefix"], "al_" + PLACEHOLDER)
    if tokens["element"] in KEYWORD_ELEMENTS:
        # A keyword element cannot be substituted bare (`void` names the
        # return type of every uninit), so rewrite its pointer SPELLINGS
        # instead: `void **` / `void**` (slot pointers and at() returns),
        # `void *const data` (the init_3 source) and `sizeof(void*)`. A
        # `(void*) x` cast stays, since none of the three shapes match it.
        keyword = r"(?<![A-Za-z0-9_])" + re.escape(tokens["element"])
        text = re.sub(keyword + r"\s*\*(?=\s*\*)", PLACEHOLDER, text)
        text = re.sub(keyword + r"\s*\*\s*const\s+data", PLACEHOLDER + " const data", text)
        text = re.sub(r"sizeof\(\s*" + re.escape(tokens["element"]) + r"\s*\*\s*\)",
                      "sizeof(" + PLACEHOLDER + ")", text)
    else:
        text = _sub_token(text, tokens["element"], PLACEHOLDER)
    text = _sub_part(text, tokens["upper"], PLACEHOLDER)

    # Whitespace runs collapse to one space so a column-aligned member or
    # assignment (`U8      *data`, `al_void.data     =`) measures as the same
    # line as its unaligned canonical form: alignment is not drift.
    lines = []
    for line in text.splitlines():
        line = " ".join(line.split())
        if line:
            lines.append(line)
    return lines


#== MARK: - Measurement ==#

def instantiations(directory):
    """Names of every al_* instantiation carrying both a .c and a .h, sorted."""
    names = set()
    for entry in os.listdir(directory):
        stem, ext = os.path.splitext(entry)
        if ext in (".c", ".h") and stem.startswith("al_"):
            names.add(stem)
    return sorted(name for name in names
                  if os.path.exists(os.path.join(directory, name + ".c"))
                  and os.path.exists(os.path.join(directory, name + ".h")))


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def load(directory, name):
    """Normalized .c lines, .h lines and tokens for one instantiation."""
    source = read(os.path.join(directory, name + ".c"))
    header = read(os.path.join(directory, name + ".h"))
    tokens = tokens_of(name, source, header)
    if tokens is None:
        return None
    return {
        "name": name,
        "tokens": tokens,
        "c": normalize(source, tokens),
        "h": normalize(header, tokens),
    }


def diff_lines(base, other, base_name, other_name, kind):
    """Unified diff of two normalized line lists, plus its differing-line count."""
    lines = list(difflib.unified_diff(
        base, other,
        fromfile=base_name + kind, tofile=other_name + kind,
        lineterm="", n=1))
    count = sum(1 for line in lines
                if (line.startswith("+") or line.startswith("-"))
                and not line.startswith("+++") and not line.startswith("---"))
    return count, lines


#== MARK: - Reporting ==#

def verdict_of(name, c_count, h_count):
    """(verdict, failed, note) for one instantiation's measured counts."""
    entry = ALLOWLIST.get(name)

    if entry is None:
        if c_count == 0 and h_count == 0:
            return "CLONE", False, "zero-diff clone of the canonical"
        known = BACKLOG.get(name)
        if known:
            return "DRIFT", True, "open backlog: " + known
        return "DRIFT", True, "not allowlisted - must be a zero-diff clone"

    if entry.get("retired"):
        return "RETIRED", False, entry["reason"]

    c_max = entry.get("c_max", 0)
    h_max = entry.get("h_max", 0)
    if c_count > c_max or h_count > h_max:
        return "OVER BUDGET", True, "budget is .c %d / .h %d - %s" % (c_max, h_max, entry["reason"])
    if c_count < c_max or h_count < h_max:
        return "RATCHET", False, "budget is .c %d / .h %d - lower it to the measured counts" % (c_max, h_max)
    return "ALLOWED", False, entry["reason"]


def main():
    parser = argparse.ArgumentParser(
        description="Diff every al_* instantiation against the canonical one.")
    parser.add_argument("--canonical", default=CANONICAL,
                        help="instantiation to normalize toward (default: %s)" % CANONICAL)
    parser.add_argument("--verbose", action="store_true",
                        help="print the diff for allowlisted files too")
    args = parser.parse_args()

    if not os.path.isdir(FAMILY_DIR):
        print("al_divergence: no such directory: %s" % FAMILY_DIR, file=sys.stderr)
        return 2

    # An EXTRA clone is measured only where its directory exists. The tool ships
    # with the family into trees that carry container/arrayList without the
    # consumer module, and a gate that crashes there guards nothing; a directory
    # that exists WITHOUT the pair, though, is the relocation-out-of-the-scan
    # case the table exists to catch, so that stays a failure.
    extra = {}
    for name, directory in sorted(EXTRA.items()):
        if not os.path.isdir(directory):
            print("note: %s lives beside its consumer in %s, which this tree does not carry - not measured"
                  % (name, os.path.relpath(directory, ROOT).replace("\\", "/")))
            continue
        extra[name] = directory
    names = sorted(set(instantiations(FAMILY_DIR)) | set(extra))
    if args.canonical not in names:
        print("al_divergence: canonical %r is not an instantiation (have: %s)"
              % (args.canonical, ", ".join(names)), file=sys.stderr)
        return 2

    loaded = {}
    unparsed = []
    for name in names:
        directory = extra.get(name, FAMILY_DIR)
        if not (os.path.isfile(os.path.join(directory, name + ".c"))
                and os.path.isfile(os.path.join(directory, name + ".h"))):
            print("al_divergence: %s is missing its .c/.h pair under %s - relocated out of the scan?"
                  % (name, os.path.relpath(directory, ROOT).replace("\\", "/")), file=sys.stderr)
            return 1
        item = load(directory, name)
        if item is None:
            unparsed.append(name)
        else:
            loaded[name] = item

    if args.canonical not in loaded:
        print("al_divergence: could not derive tokens for canonical %r" % args.canonical,
              file=sys.stderr)
        return 2

    # The recorded budgets were measured against CANONICAL. Diffing toward any
    # other instantiation is an exploration aid (al_string against its real twin
    # al_str, say), so it reports but never gates.
    gating = args.canonical == CANONICAL

    base = loaded[args.canonical]
    rows = []
    failures = []
    allowed_diffs = []
    macro_drift = []

    for name in names:
        if name == args.canonical or name not in loaded:
            continue
        item = loaded[name]
        c_count, c_diff = diff_lines(base["c"], item["c"], args.canonical, name, ".c")
        h_count, h_diff = diff_lines(base["h"], item["h"], args.canonical, name, ".h")
        verdict, failed, note = verdict_of(name, c_count, h_count)
        rows.append((name, c_count, h_count, verdict, note))
        if failed and gating:
            failures.append((name, verdict, c_diff, h_diff))
        elif c_count or h_count:
            allowed_diffs.append((name, verdict, c_diff, h_diff))

        expected_macro = "_AL_%s_GROWTH_FACTOR" % item["tokens"]["upper"]
        actual_macro = item["tokens"]["growth"]
        if actual_macro and actual_macro != expected_macro:
            macro_drift.append((name, actual_macro, expected_macro))

    if not gating:
        print("exploration mode: budgets were measured against %s, so nothing gates here."
              % CANONICAL)
    print("canonical: %s   family: %d instantiations in %s"
          % (args.canonical, len(names), os.path.relpath(FAMILY_DIR, ROOT).replace("\\", "/")))
    print()
    print("%-20s %6s %6s  %-12s %s" % ("instantiation", ".c", ".h", "verdict", "note"))
    print("%-20s %6s %6s  %-12s %s" % ("-" * 20, "-" * 6, "-" * 6, "-" * 12, "-" * 44))
    for name, c_count, h_count, verdict, note in rows:
        print("%-20s %6d %6d  %-12s %s" % (name, c_count, h_count, verdict, note))
    print()

    for name, actual_macro, expected_macro in macro_drift:
        print("note: %s growth macro is %s, the family shape is %s"
              % (name, actual_macro, expected_macro))
    if unparsed:
        print("note: could not derive tokens for: %s" % ", ".join(unparsed))
    if macro_drift or unparsed:
        print()

    if args.verbose:
        for name, verdict, c_diff, h_diff in allowed_diffs:
            print("=" * 72)
            print("ALLOWED DIVERGENCE: %s (%s)" % (name, verdict))
            print("=" * 72)
            for line in c_diff + h_diff:
                print(line)
            print()

    if not failures:
        if gating:
            print("OK - every instantiation is within its allowlisted budget.")
        return 0

    for name, verdict, c_diff, h_diff in failures:
        print("=" * 72)
        print("%s: %s" % (verdict, name))
        print("=" * 72)
        for line in c_diff + h_diff:
            print(line)
        print()
    print("FAIL - %d instantiation(s) diverge outside the allowlist: %s"
          % (len(failures), ", ".join(name for name, _, _, _ in failures)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
