#!/usr/bin/env python3
# Copyright 2026 Davide Faconti
# SPDX-License-Identifier: Apache-2.0
"""CI gate for the host-surface inventory and feature_floors.json.

Fails when a host surface exists in the headers but is not classified, so a
new tail slot / service / flag cannot merge without its floor entry. Also
validates the table itself. Exit 0 = consistent.
"""

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_host_surfaces import extract  # noqa: E402

VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")


def version_tuple(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in text.split("."))


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    errors: list[str] = []

    inventory = extract(repo)
    snapshot_path = repo / "tools/feature_floors/host_surfaces.snapshot"
    snapshot = snapshot_path.read_text().split()
    if inventory != snapshot:
        added = sorted(set(inventory) - set(snapshot))
        removed = sorted(set(snapshot) - set(inventory))
        for surface in added:
            errors.append(
                f"NEW host surface not classified: {surface}\n"
                f"  -> add it to pj_base/feature_floors.json (a 'surfaces' entry with its "
                f"introducing release, or 'baseline' if it predates the minimum supported floor)\n"
                f"  -> then refresh the snapshot: tools/feature_floors/extract_host_surfaces.py --write"
            )
        for surface in removed:
            errors.append(f"host surface removed from headers but still in snapshot: {surface}")

    table_path = repo / "pj_base/feature_floors.json"
    table = json.loads(table_path.read_text())
    sdk_version = (repo / "VERSION").read_text().strip()
    floor_min = table.get("minimum_supported_floor", "")
    if not VERSION_RE.match(floor_min):
        errors.append(f"minimum_supported_floor malformed: {floor_min!r}")

    surfaces = table.get("surfaces", {})
    baseline = table.get("baseline", [])
    declarations = set()
    unreleased: list[str] = []
    for identifier, entry in surfaces.items():
        since = entry.get("since", "")
        declaration = entry.get("declaration", "")
        if not identifier:
            errors.append("empty surface identifier")
        if not VERSION_RE.match(since):
            errors.append(f"{identifier}: malformed since {since!r}")
        else:
            if VERSION_RE.match(floor_min) and version_tuple(since) < version_tuple(floor_min):
                errors.append(f"{identifier}: since {since} below minimum_supported_floor {floor_min}")
            # since may exceed VERSION during development: a surface added for
            # the next, not-yet-released version. That requires the changelog's
            # unreleased section to declare the host-contract extension.
            if version_tuple(since) > version_tuple(sdk_version):
                unreleased.append(identifier)
        if not isinstance(entry.get("negotiated"), bool):
            errors.append(f"{identifier}: negotiated must be a boolean")
        if not declaration:
            errors.append(f"{identifier}: empty declaration")
        declarations.add(declaration)

    if unreleased:
        changelog = (repo / "CHANGELOG.md").read_text()
        if "Host contract: extended" not in changelog:
            errors.append(
                "surfaces newer than VERSION exist (" + ", ".join(sorted(unreleased)) + ") but "
                "CHANGELOG.md has no 'Host contract: extended: ...' line in the unreleased section"
            )

    # Every inventoried surface is classified exactly once: as some surface's
    # declaration, or as baseline. Nothing is silently ignored.
    classified = declarations | set(baseline)
    for surface in snapshot:
        if surface not in classified:
            errors.append(f"snapshot surface neither a declaration nor baseline: {surface}")
    for surface in sorted(classified - set(snapshot)):
        errors.append(f"classified surface no longer in the snapshot: {surface}")
    overlap = declarations & set(baseline)
    for surface in sorted(overlap):
        errors.append(f"surface classified twice (declaration AND baseline): {surface}")

    if errors:
        print("feature-floors check FAILED:\n" + "\n".join(errors))
        return 1
    print(f"feature-floors check OK: {len(snapshot)} surfaces, {len(surfaces)} above-floor entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
