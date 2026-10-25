#!/usr/bin/env python3
"""Correct v4's mistaken NeoCity logo selection and undo its eye regression.

Use from the MapleStory-Client root:
    python fix_v83_neocity_logo_eye_v5.py --root .           # preview
    python fix_v83_neocity_logo_eye_v5.py --root . --apply   # backup + edit

Only changes three NeoCity sprite coordinates in UIWorldSelect.cpp.
No input, network, or character-selection code is modified.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from datetime import datetime
from pathlib import Path

FILE = Path("IO/UITypes/UIWorldSelect.cpp")
# node 3: independent blinking-eye overlay; return to the visually better v3 position.
EYE = (545, 336)
# node 5: actual NeoCity wordmark; 25 px left and 20 px down from v4's screenshot.
LOGO = (235, 405)
# node 4 is NOT the logo; undo the unintended v4 movement of its decoration.
DECORATION = (155, 460)
MARKER = "V83_NEOCITY_LOGO_EYE_V5"


def replace_node_position(
    src: str, node: str, target: tuple[int, int], accepted: set[tuple[int, int]]
) -> tuple[str, tuple[int, int], bool]:
    # Match the actual constructor, not an unrelated comment or hardcoded draw call.
    pattern = re.compile(
        r'(sprites\.emplace_back\(\s*neo\["'
        + re.escape(node)
        + r'"\]\s*,\s*Point<int16_t>\(\s*)'
        + r"(-?\d+)\s*,\s*(-?\d+)"
        + r"(\s*\)\s*\);)"
    )
    matches = list(pattern.finditer(src))
    if len(matches) != 1:
        raise ValueError(
            f"Expected exactly one NeoCity node {node} sprite; found {len(matches)}"
        )
    m = matches[0]
    before = (int(m.group(2)), int(m.group(3)))
    if before not in accepted and before != target:
        raise ValueError(f"NeoCity node {node} at unexpected {before}; no changes made")
    if before == target:
        return src, before, False
    replacement = f"{m.group(1)}{target[0]}, {target[1]}{m.group(4)}"
    return src[: m.start()] + replacement + src[m.end() :], before, True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    root = args.root.expanduser().resolve()
    path = root / FILE
    try:
        original = path.read_text(encoding="utf-8")
        revised, old_eye, eye_changed = replace_node_position(
            original, "3", EYE, {(535, 332), (540, 336)}
        )
        revised, old_decoration, decor_changed = replace_node_position(
            revised, "4", DECORATION, {(130, 480), (155, 460)}
        )
        revised, old_logo, logo_changed = replace_node_position(
            revised, "5", LOGO, {(310, 385), (285, 405)}
        )

        print("v83 NeoCity logo/eye correction")
        print("Project:", root)
        print("Mode:", "APPLY" if args.apply else "DRY RUN")
        print(
            f"  neoCity/3 eye:  {old_eye} -> {EYE}"
            + (" [unchanged]" if not eye_changed else "")
        )
        print(
            f"  neoCity/5 logo: {old_logo} -> {LOGO}"
            + (" [unchanged]" if not logo_changed else "")
        )
        print(
            f"  neoCity/4 decoration: {old_decoration} -> {DECORATION}"
            + (" [unchanged]" if not decor_changed else "")
        )

        if revised == original:
            print("No changes needed.")
            return 0
        if not args.apply:
            print("Dry run passed. Rerun with --apply to write the changes.")
            return 0

        backup_dir = root / (
            "v83-neocity-v5-backup-" + datetime.now().strftime("%Y%m%d-%H%M%S")
        )
        backup_path = backup_dir / FILE
        backup_path.parent.mkdir(parents=True, exist_ok=False)
        shutil.copy2(path, backup_path)
        temp = path.with_name(path.name + ".v83v5.tmp")
        temp.write_text(revised, encoding="utf-8")
        temp.replace(path)
        print("Applied. Backup:", backup_path)
        return 0
    except (ValueError, OSError) as exc:
        print("Stopped without modifying source:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
