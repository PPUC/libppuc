#!/usr/bin/env python3
"""Checks that config-tool and ppuc-pinmame agree on the GameCore schema.

The `emGame:`, `tilt:` and `ballSave:` blocks are described in two places that
cannot see each other:

  * the exporter, config-tool/.../GamesController.php, which writes them
  * the parser, ppuc/src/game/GameConfigYaml.cpp, which reads them

A key added to one and not the other fails silently: the exporter writes
something nobody reads, or an operator sets a value in the UI that never
reaches the machine. This is the same hazard check-schema-drift.py covers for
the hardware schema, for the schema that came after it.

Usage, from the workspace root that holds both checkouts:

    python3 libppuc/tools/check-gamecore-drift.py [--strict]

Without --strict it reports and exits 0, because a key with a sensible default
on the parser side is a legitimate thing not to expose in the UI yet.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

# Keys the parser reads that the exporter deliberately does not write, because
# the parser has a sensible default and the UI does not need to expose them.
# Anything not listed here and not written is reported.
KNOWN_UNEXPORTED = {
    "emGame": {
        # Presentation timings nobody has needed to change per machine yet.
        "attractPageMs": "parser default is fine; not worth a field",
        "gameOverHoldMs": "parser default is fine; not worth a field",
        # GI wiring is described by the ledStripes/pwmOutput blocks already.
        "giStrings": "derived from the machine's GI wiring, not a game setting",
        "giOnLevel": "parser default; a machine wanting less is unusual",
        "knockerPulseMs": "the board enforces its own pulse envelope anyway",
        "maxCredits": "parser default of 99 is the usual maximum",
        # Friendly names are authored as Lua, not as a game field, until there
        # is a UI for naming devices.
        "names": "not authored in the UI yet; rules may still use raw numbers",
        "enabled": "implied by engine: gamecore",
    },
    "emGame.tilt": {
        "extraBallSurvivesTilt": "parser default of false is the usual rule",
        "recoverTimeoutMs": "safety net only; the parser default is fine",
        "slamEndsGame": "parser default of true is the only sensible behaviour",
    },
    "tilt": {
        "warningLampMs": "parser default; a fixed flash length is fine",
    },
    "ballSave": {
        "durationMs": "the exporter writes `seconds`, which the parser prefers",
        "kickPulseMs": "the board enforces its own pulse envelope anyway",
        "onlyOnBalls": "not authored in the UI yet",
    },
}

CPP = "ppuc/src/game/GameConfigYaml.cpp"
PHP = "config-tool/web/modules/custom/ppuc_games/src/Controller/GamesController.php"

# Each block: the C++ function and local it is read from, and the PHP function
# that writes it.
#
# The C++ function matters: `tilt` is a local in BOTH LoadGameConfigFromYaml
# (the GameCore-only emGame.tilt sub-block) and LoadPlayfieldAssistFromYaml (the
# shared top-level tilt block). Comparing them against each other would report
# drift that does not exist and hide drift that does.
BLOCKS = {
    "emGame": ("LoadGameConfigFromYaml", "em", "buildEmGameYaml"),
    "emGame.tilt": ("LoadGameConfigFromYaml", "tilt", "buildEmGameYaml:tilt"),
    "tilt": ("LoadPlayfieldAssistFromYaml", "tilt", "buildTiltYaml"),
    "ballSave": ("LoadPlayfieldAssistFromYaml", "save", "buildBallSaveYaml"),
}

# Sub-blocks the exporter builds inline. Their keys are read from a different
# C++ local, so comparing them against the parent block would be nonsense.
NESTED = {
    "trough", "shooterLane", "tilt", "replay", "match", "names",
    "switch", "switches", "kickCoil", "kickPulseMs", "settleMs",
    "thresholds", "awardCredit", "enabled", "awardExtraBall",
    "inhibitSwitch", "giOff", "endsBallOnly", "skipBonus", "coils", "lamps",
}


def cpp_function_body(source: str, name: str) -> str:
    """The body of a C++ free function, so locals in other functions do not leak in."""
    signature = re.search(rf'^\w[\w:<>,&*\s]*\b{name}\(', source, re.M)
    assert signature, f"C++ function not found: {name}"
    i = source.index("{", signature.start())
    depth, j = 0, i
    while True:
        if source[j] == "{":
            depth += 1
        elif source[j] == "}":
            depth -= 1
            if depth == 0:
                return source[i : j + 1]
        j += 1


def parser_keys(source: str, function: str, local: str) -> set[str]:
    """Keys the C++ parser reads out of `local`, within `function` only."""
    source = cpp_function_body(source, function)
    keys: set[str] = set()
    for pattern in (
        rf'Read<[^>]+>\(\s*{local},\s*"([A-Za-z0-9_]+)"',
        rf'ReadIntList\(\s*{local},\s*"([A-Za-z0-9_]+)"',
        rf'{local}\["([A-Za-z0-9_]+)"\]',
    ):
        keys.update(re.findall(pattern, source))
    return keys


def php_function_body(source: str, name: str) -> str:
    start = source.index(f"protected function {name}(")
    i = source.index("{", start)
    depth, j = 0, i
    while True:
        if source[j] == "{":
            depth += 1
        elif source[j] == "}":
            depth -= 1
            if depth == 0:
                return source[i : j + 1]
        j += 1


def exporter_keys(source: str, spec: str) -> set[str]:
    """Keys the exporter writes, in either array style.

    Both `'key' => value` inside an array literal and `$block['key'] = value`
    assignment are used in the exporter, and missing the second form would make
    this check silently pass for whole blocks.
    """
    # "function:local" narrows to a sub-array built inside that function, for a
    # nested block such as emGame.tilt.
    function, _, local = spec.partition(":")
    body = php_function_body(source, function)
    if local:
        start = body.index(f"${local} = [")
        end = body.index("];", start)
        sub = body[start:end]
        keys = set(re.findall(r"'([a-zA-Z][A-Za-z0-9_]*)'\s*=>", sub))
        keys.update(re.findall(rf"\${local}\['([a-zA-Z][A-Za-z0-9_]*)'\]\s*=", body))
        return keys
    keys = set(re.findall(r"'([a-zA-Z][A-Za-z0-9_]*)'\s*=>", body))
    keys.update(re.findall(r"\$[A-Za-z_][A-Za-z0-9_]*\['([a-zA-Z][A-Za-z0-9_]*)'\]\s*=", body))
    return keys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on any drift, for CI")
    parser.add_argument("--root", default=".",
                        help="workspace root holding the ppuc and config-tool checkouts")
    args = parser.parse_args()

    cpp_path = os.path.join(args.root, CPP)
    php_path = os.path.join(args.root, PHP)
    for path in (cpp_path, php_path):
        if not os.path.exists(path):
            print(f"not found: {path}", file=sys.stderr)
            print("run this from the workspace root, or pass --root", file=sys.stderr)
            return 2

    cpp = open(cpp_path).read()
    php = open(php_path).read()

    problems = 0
    for block, (cpp_function, local, php_spec) in BLOCKS.items():
        read = parser_keys(cpp, cpp_function, local)
        written = exporter_keys(php, php_spec)

        # Written but never read: an operator sets it and nothing happens.
        orphans = sorted(k for k in written - read if k not in NESTED)
        # Read but never written: usually fine, if it has a default.
        missing = sorted(k for k in read - written
                         if k not in NESTED and k not in KNOWN_UNEXPORTED.get(block, {}))

        print(f"{block}: exporter writes {len(written)}, parser reads {len(read)}")
        if orphans:
            problems += len(orphans)
            print(f"  DRIFT: written but never read by ppuc-pinmame: {orphans}")
        if missing:
            problems += len(missing)
            print(f"  DRIFT: read by ppuc-pinmame but never exported: {missing}")
        if not orphans and not missing:
            print("  in sync")

    print()
    if problems:
        print(f"{problems} key(s) out of sync.")
        print("Add the key to the other side, or record it in KNOWN_UNEXPORTED with a reason.")
        return 1 if args.strict else 0

    print("config-tool and ppuc-pinmame agree on the GameCore schema.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
