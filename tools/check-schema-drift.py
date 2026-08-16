#!/usr/bin/env python3
"""Compares a real config-tool export against what libppuc's validator knows.

The same schema is written down in three places - the config-tool exporter, the
validator in src/PPUC.cpp, and the firmware's config topics - with nothing
shared between them. Nothing catches a key that one side starts writing and
another never learned about: the YAML loads, the field is ignored, and the
feature quietly does nothing.

This is the cheap half of that check. It reads the keys out of an actual
exported game and the keys out of the validator, and reports:

  * keys in the export the validator never names - either a new field libppuc
    has not caught up with, or a typo that will be silently ignored forever;
  * keys the validator names that no export contains - usually an optional
    feature nothing has used yet, which is why those need an explicit reason
    below rather than a failure.

It deliberately parses rather than executes: running the exporter needs a
database, and this has to be cheap enough for CI.

Both directions only make sense over every export there is. Checking one file
asks "does any game use this key", and the answer for a single game is no for
most of them. Pass --new-export to check just the first direction, which is what
fits a game that was only just created.

Usage:
    check-schema-drift.py [--new-export] <exported-game.yml> [more.yml ...]
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Keys the validator names that a game export need not contain, with the reason
# it is absent. Anything not listed here has to be explained or fixed.
KNOWN_ABSENT = {
    "coilGiMappings": "optional, SYS11 only; no exported game uses it yet",
    "switchMatrix": "optional; only games with an original matrix harness",
    "dualWinding": "optional; games must be re-exported since config-tool gained the field",
    "eosSwitch": "optional; same as dualWinding",
    "holdWinding": "optional; no game has a separately driven flipper pair yet",
    "stopSwitches": "optional; no exported game has a flipper EOS or a motor yet",
    "slowSwitches": "optional; no game has marked a board yet",
    "options": "optional LED effect field",
    "fadeRate": "optional LED effect field",
    "gamma": "optional LED strip field",
    "size": "optional LED strip field",
    "root": "used in validator error paths, not a config key",
    "debounce_mode": "accepted spelling variant of debounceMode",
    "switchGroups": "optional; only games with Lua rules that use groups",
    "buttons": "reserved switchGroups name, built from button: true switches",
    # switchMatrix sub-fields. Absent for the same reason the section is.
    "activeLow": "switchMatrix sub-field",
    "rows": "switchMatrix sub-field",
    # Effect trigger and LED sub-fields that these games do not use.
    "coil": "coilGiMappings sub-field",
    "source": "effect trigger sub-field; these games use named triggers",
    "value": "effect trigger sub-field",
    "simpleTrigger": "optional effect trigger form",
    "colors": "multi-colour effect field; these games set a single color",
    "onBrightness": "optional LED field",
    "offBrightness": "optional LED field",
}

# Keys an export contains that the validator is not expected to name.
KNOWN_UNVALIDATED = {
    "ppucVersion": "written by config-tool for provenance; libppuc ignores it",
    # Checked 2026-08-10: nothing in libppuc or ppuc reads either of these.
    # config-tool writes them, and no consumer in this stack looks at them.
    # Left as-is rather than removed, because something outside the stack may.
    "dipSwitches": "exported but read by nothing in libppuc or ppuc",
    "mechs": "exported but read by nothing in libppuc or ppuc",
    # Host-side blocks, read by ppuc-pinmame and deliberately ignored here.
    # See HOST_SIDE_BLOCKS below for why their contents are not checked either.
    "emGame": "read by ppuc-pinmame (GameCore); libppuc ignores it by design",
    "tilt": "read by ppuc-pinmame (tilt warnings); libppuc ignores it by design",
    "ballSave": "read by ppuc-pinmame (ball save); libppuc ignores it by design",
}

# Top-level blocks describing the game rather than the wiring.
#
# ppuc-pinmame parses these; libppuc never validates them, and that is the whole
# point -- it is what lets the ROM-less schema evolve without a libppuc release
# and a SHA pin bump. Checking their contents here would report every key inside
# them as drift forever, so the subtree is skipped and the block names are
# recorded above.
#
# They are not unchecked, just checked elsewhere:
# libppuc/tools/check-gamecore-drift.py compares them against the exporter.
HOST_SIDE_BLOCKS = {"emGame", "tilt", "ballSave"}

# Blocks whose child keys are not schema at all.
#
# switchGroups is validated by libppuc, but its children are names the operator
# chose -- "slingshots", "targets" -- and no validator can ever name those. The
# first export to use switch groups would otherwise report each group name as
# drift forever.
NAMED_BY_OPERATOR = {"switchGroups"}

# Every block whose contents this checker skips, for either reason.
SKIP_SUBTREE_OF = HOST_SIDE_BLOCKS | NAMED_BY_OPERATOR


def validated_keys() -> set:
    """Key names src/PPUC.cpp validates."""
    text = (ROOT / "src" / "PPUC.cpp").read_text()
    keys = set()
    # ValidateRequiredField<T>(node, path, "key") and the Optional variant
    keys |= set(re.findall(r'Validate\w*Field<[^>]*>\(\s*[^,]+,\s*[^,]+,\s*"([^"]+)"', text))
    # ValidateRequiredMap/Sequence/Items(node, "key", ...)
    keys |= set(re.findall(r'Validate\w*(?:Map|Sequence|Items)\(\s*[^,]+,\s*"([^"]+)"', text))
    # Helpers that take the key as a plain second argument, e.g.
    # ParseRequiredHexColorField(item, "color", ...). Missing these reads as
    # drift when there is none - "color" was the first false positive here.
    keys |= set(re.findall(r'Parse\w+\(\s*[^,()]+,\s*"([^"]+)"', text))
    # Direct lookups: config["key"] / item["key"]
    keys |= set(re.findall(r'\w+\[\s*"([a-zA-Z][a-zA-Z0-9_]*)"\s*\]', text))
    return keys


def export_keys(path: Path) -> set:
    """Every mapping key in an exported game, at any depth.

    Read with a regex rather than a YAML parser so this has no dependencies;
    the exporter writes one key per line, which is all that has to hold.
    """
    keys = set()
    skip_deeper_than = None
    for line in path.read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue

        indent = len(line) - len(line.lstrip())
        # Inside a host-side block: skip until the indentation returns to the
        # level of the block key itself.
        if skip_deeper_than is not None:
            if indent > skip_deeper_than:
                continue
            skip_deeper_than = None

        match = re.match(r"\s*(?:-\s+)?([A-Za-z][A-Za-z0-9_]*):", line)
        if not match:
            continue

        key = match.group(1)
        keys.add(key)
        if indent == 0 and key in SKIP_SUBTREE_OF:
            skip_deeper_than = indent
    return keys


def main(argv) -> int:
    args = argv[1:]
    new_export = "--new-export" in args
    args = [a for a in args if a != "--new-export"]
    if not args:
        print(__doc__)
        return 2

    exports = [Path(p) for p in args]
    for path in exports:
        if not path.is_file():
            print(f"error: {path} does not exist")
            return 2

    validated = validated_keys()
    emitted = set()
    for path in exports:
        emitted |= export_keys(path)

    problems = []

    for key in sorted(emitted - validated):
        if key in KNOWN_UNVALIDATED:
            continue
        problems.append(
            f"'{key}' appears in an export but the validator never names it. "
            f"libppuc will ignore it silently."
        )

    for key in sorted(validated - emitted) if not new_export else []:
        if key in KNOWN_ABSENT:
            continue
        problems.append(
            f"'{key}' is validated but appears in no export. If config-tool "
            f"cannot produce it, the field is unreachable; if it can, add an "
            f"export that uses it."
        )

    print(f"validator knows {len(validated)} keys; "
          f"exports contain {len(emitted)} across {len(exports)} file(s)"
          + (" (checking exported keys only)" if new_export else ""))

    if problems:
        print("\nSchema drift between config-tool and libppuc:\n")
        for problem in problems:
            print(f"  - {problem}")
        print("\nFix the side that is wrong, or record the reason in "
              "tools/check-schema-drift.py.")
        return 1

    if new_export:
        print("No drift: every key in this export is one the validator knows.")
    else:
        print("No drift: every exported key is validated, and every validated "
              "key is either exported or explained.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
