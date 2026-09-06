#!/usr/bin/env python3
# Copyright 2026 Davide Faconti
# SPDX-License-Identifier: Apache-2.0
"""Inventory the SDK's HOST-CONTRACT surfaces from the protocol headers.

The inventory is what feature_floors.json must classify: every surface a
plugin can require from the HOST at runtime. Three kinds are extracted:

  1. Function-pointer members of host-side vtables (structs named
     PJ_*host_vtable_t, plus PJ_service_registry_vtable_t) in
     pj_base/include/pj_base/*.h — emitted as "Struct::member".
  2. Versioned service/extension id strings ("pj.<name>.v<N>") in the
     pj_base and pj_plugins public headers. Diagnostic-code strings and the
     "pj.experimental." prefix don't version a host contract and are excluded
     by the .v<N> suffix rule.
  3. PJ_*_FLAG* macros in the protocol headers (wire-contract flags a host
     must understand).

Plugin-side vtables (PJ_data_source_vtable_t, PJ_dialog_vtable_t, ...) are
deliberately absent: the HOST calls those, so an old host simply not calling
a new plugin slot degrades by construction and never constrains a plugin's
min_sdk_required. Client-side static-library helpers are equally absent —
they travel inside the plugin binary.

Run with --write to refresh host_surfaces.snapshot; CI diffs a fresh
extraction against the checked-in snapshot (see check_feature_floors.py).
"""

import argparse
import re
import sys
from pathlib import Path

HOST_VTABLE_RE = re.compile(r"typedef struct (PJ_\w*host_vtable_t|PJ_service_registry_vtable_t)\s*\{")
# One function-pointer member: "<ret> (*name)(...)" possibly split over lines.
MEMBER_RE = re.compile(r"\(\s*\*\s*(\w+)\s*\)\s*\(")
SERVICE_ID_RE = re.compile(r'"(pj\.[a-z_0-9.]+\.v\d+)"')
FLAG_RE = re.compile(r"#define\s+(PJ_\w*_FLAG\w*)\s")

PROTOCOL_HEADER_GLOBS = [
    ("pj_base/include/pj_base", "*.h"),
]
SERVICE_ID_GLOBS = [
    ("pj_base/include/pj_base", "**/*.h*"),
    ("pj_plugins/include", "**/*.h*"),
    ("pj_plugins/dialog_protocol/include", "**/*.h*"),
]


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def vtable_members(text: str) -> list[str]:
    surfaces = []
    for match in HOST_VTABLE_RE.finditer(text):
        struct_name = match.group(1)
        # Body runs to the closing "} <name>;" — find the matching brace by depth.
        depth = 0
        start = text.index("{", match.start())
        for pos in range(start, len(text)):
            if text[pos] == "{":
                depth += 1
            elif text[pos] == "}":
                depth -= 1
                if depth == 0:
                    body = text[start:pos]
                    break
        else:
            raise SystemExit(f"unterminated struct {struct_name}")
        surfaces.extend(f"{struct_name}::{m}" for m in MEMBER_RE.findall(body))
    return surfaces


def extract(repo: Path) -> list[str]:
    surfaces: set[str] = set()
    for rel, glob in PROTOCOL_HEADER_GLOBS:
        for header in sorted((repo / rel).glob(glob)):
            text = strip_comments(header.read_text())
            surfaces.update(vtable_members(text))
            surfaces.update(FLAG_RE.findall(text))
    for rel, glob in SERVICE_ID_GLOBS:
        for header in sorted((repo / rel).glob(glob)):
            surfaces.update(SERVICE_ID_RE.findall(strip_comments(header.read_text())))
    return sorted(surfaces)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--write", action="store_true", help="refresh host_surfaces.snapshot")
    args = parser.parse_args()

    inventory = extract(args.repo)
    snapshot = args.repo / "tools/feature_floors/host_surfaces.snapshot"
    if args.write:
        snapshot.write_text("\n".join(inventory) + "\n")
        print(f"wrote {len(inventory)} surfaces to {snapshot}")
        return 0
    sys.stdout.write("\n".join(inventory) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
