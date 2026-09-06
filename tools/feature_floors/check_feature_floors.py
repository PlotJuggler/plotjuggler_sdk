#!/usr/bin/env python3
# Copyright 2026 Davide Faconti
# SPDX-License-Identifier: Apache-2.0
"""CI gate for the host-surface inventory and feature_floors.json.

Fails when a host surface exists in the headers but is not classified, so a
new tail slot / service / flag cannot merge without its floor entry. Table
schema validation itself lives in feature_floor_check.py (the shipped,
reusable parser); this gate layers the repo-side obligations on top:
snapshot/classification consistency, unique release assignment, and the
changelog coupling. Exit 0 = consistent.
"""

import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_host_surfaces import extract  # noqa: E402
from feature_floor_check import CheckError, format_version, load_surface_table, parse_version  # noqa: E402


def _unreleased_changelog_section(changelog: str) -> str:
    """The text of the unreleased section, or empty when none exists.

    Restricting the 'Host contract:' obligation to this section keeps a
    historical extension line from satisfying a new release's duty. Residual
    gap, accepted: bumping VERSION to the surface's release in the same PR
    retires the obligation — the release commit is where that is reviewed.
    """
    headings = list(re.finditer(r"^## .*$", changelog, flags=re.MULTILINE))
    for index, heading in enumerate(headings):
        if "unreleased" in heading.group(0).lower():
            start = heading.end()
            end = headings[index + 1].start() if index + 1 < len(headings) else len(changelog)
            return changelog[start:end]
    return ""


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
                f"  -> add it to pj_base/feature_floors.json (under 'since'/<introducing release>, "
                f"or 'baseline' if it predates the minimum supported floor)\n"
                f"  -> then refresh the snapshot: tools/feature_floors/extract_host_surfaces.py --write"
            )
        for surface in removed:
            errors.append(f"host surface removed from headers but still in snapshot: {surface}")
        if not added and not removed:
            errors.append("snapshot is stale (ordering): refresh with extract_host_surfaces.py --write")

    table_path = repo / "pj_base/feature_floors.json"
    sdk_version = parse_version((repo / "VERSION").read_text().strip(), "VERSION file")
    try:
        table = load_surface_table(table_path)
    except CheckError as exc:
        print(f"feature-floors check FAILED:\n{exc}")
        return 1

    above_floor = [
        surface for surface in table.surfaces.values() if surface.identifier not in table.baseline
    ]

    # Unique release assignment: the parser already rejects an identifier in
    # two groups and duplicate baseline entries; a declaration reused under a
    # second identifier would still assign two versions to one ABI entity.
    declaration_counts = Counter(surface.declaration for surface in above_floor)
    declaration_counts.update(table.baseline)
    for declaration, count in sorted(declaration_counts.items()):
        if count > 1:
            errors.append(f"surface declaration classified {count} times: {declaration}")

    # since may exceed VERSION during development: a surface added for the
    # next, not-yet-released version. That requires the changelog's unreleased
    # section to declare the host-contract extension.
    unreleased = sorted(
        surface.identifier for surface in above_floor if surface.since > sdk_version
    )
    if unreleased:
        section = _unreleased_changelog_section((repo / "CHANGELOG.md").read_text())
        if "Host contract: extended" not in section:
            errors.append(
                "surfaces newer than VERSION exist (" + ", ".join(unreleased) + ") but the "
                "unreleased CHANGELOG.md section has no 'Host contract: extended: ...' line"
            )

    # Every inventoried surface is classified exactly once: as some surface's
    # declaration, or as baseline. Nothing is silently ignored.
    classified = {surface.declaration for surface in above_floor} | set(table.baseline)
    for surface in snapshot:
        if surface not in classified:
            errors.append(f"snapshot surface neither a declaration nor baseline: {surface}")
    for surface in sorted(classified - set(snapshot)):
        errors.append(f"classified surface no longer in the snapshot: {surface}")

    if errors:
        print("feature-floors check FAILED:\n" + "\n".join(errors))
        return 1
    print(
        f"feature-floors check OK: {len(snapshot)} surfaces, {len(above_floor)} above-floor entries "
        f"(minimum {format_version(table.minimum_supported_floor)})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
