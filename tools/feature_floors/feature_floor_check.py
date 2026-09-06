#!/usr/bin/env python3
# Copyright 2026 Davide Faconti
# SPDX-License-Identifier: Apache-2.0
"""Reusable min_sdk_required floor checking over feature_floors.json.

This module is the single owner of the table schema and of the rules a plugin
floor claim is validated against. It ships with the SDK package next to
feature_floors.json so any plugin author — in-tree or third-party — can run
the same check the official-plugins CI runs:

    python3 feature_floor_check.py --table feature_floors.json \
        --manifest my_plugin/manifest.json --sources my_plugin

The contract: ``min_sdk_required`` (the functioning floor) must cover every
matched NON-negotiated surface — a hard surface above the floor is always a
failure. When any matched surface's introducing release exceeds the floor, the
manifest must declare ``suggested_sdk_version`` (the full-feature floor —
exactly max(since) over all matched surfaces) plus ``floor_test`` (one gtest,
existing in the plugin's tests, proving the plugin functions against a
floor-level host); both are forbidden when nothing exceeds the floor. Floors
below the table's ``minimum_supported_floor`` fail; over-declared floors are
the caller's business (this module reports the effective maximum so drivers
can warn).

Scanning is comment- and raw-string-aware token matching with identifier
boundaries; the host-surface universe is a few dozen identifiers, so token
precision is sufficient. A Clang-based extractor is the documented upgrade
path if a real ambiguity ever appears.

Standard library only. No repository-layout assumptions: callers hand in the
table path, source directories, and test directories.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ipp",
    ".tpp",
}
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
RAW_STRING_START_RE = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(')


class CheckError(Exception):
    """A configuration or input shape error that must fail the check."""


def parse_version(value: object, context: str) -> tuple[int, int, int]:
    """Parse a plain X.Y.Z release version (the only form floors may name)."""
    if not isinstance(value, str) or not VERSION_RE.fullmatch(value):
        raise CheckError(f"{context} must name a stable release as X.Y.Z, got {value!r}")
    parts = value.split(".")
    return int(parts[0]), int(parts[1]), int(parts[2])


def format_version(version: tuple[int, int, int]) -> str:
    return ".".join(str(part) for part in version)


@dataclass(frozen=True)
class Surface:
    identifier: str
    since: tuple[int, int, int]
    negotiated: bool
    declaration: str
    matcher: re.Pattern[str]

    @property
    def since_text(self) -> str:
        return format_version(self.since)


@dataclass(frozen=True)
class SurfaceTable:
    surfaces: dict[str, Surface]
    baseline: frozenset[str]
    minimum_supported_floor: tuple[int, int, int]
    path: Path

    @property
    def minimum_text(self) -> str:
        return format_version(self.minimum_supported_floor)


def _reject_duplicate_json_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise CheckError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def surface_matcher(identifier: str) -> re.Pattern[str]:
    if not identifier:
        raise CheckError("surface identifiers must not be empty")
    pattern = re.escape(identifier)
    if identifier[0].isalnum() or identifier[0] == "_":
        pattern = r"(?<![A-Za-z0-9_])" + pattern
    if identifier[-1].isalnum() or identifier[-1] == "_":
        pattern += r"(?![A-Za-z0-9_])"
    return re.compile(pattern)


def load_surface_table(path: Path) -> SurfaceTable:
    """Parse the compact version-grouped table (the only schema parser).

    Shape: ``{"schema_version": 1, "minimum_supported_floor": "X.Y.Z",
    "since": {"X.Y.Z": {identifier: entry}}, "baseline": [identifier]}``
    where entry is a declaration string (``""`` = the declaration IS the
    identifier, as with flag macros) or ``{"declaration": ...,
    "negotiated": false}`` for a hard surface. ``negotiated`` defaults true.
    """
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise CheckError(f"cannot read surface table {path}: {exc}") from exc
    try:
        document = json.loads(raw, object_pairs_hook=_reject_duplicate_json_keys)
    except json.JSONDecodeError as exc:
        raise CheckError(f"invalid JSON in surface table {path}: {exc}") from exc

    if not isinstance(document, dict):
        raise CheckError(f"surface table {path} must contain a JSON object")
    expected_keys = {"schema_version", "minimum_supported_floor", "_comment", "since", "baseline"}
    unknown_keys = set(document) - expected_keys
    if unknown_keys:
        raise CheckError(
            f"surface table {path} has unknown top-level keys: {', '.join(sorted(unknown_keys))}"
        )
    if document.get("schema_version") != 1:
        raise CheckError(f"surface table {path} must use schema_version 1")

    minimum = parse_version(
        document.get("minimum_supported_floor"), f"minimum_supported_floor in {path}"
    )

    since_raw = document.get("since")
    if not isinstance(since_raw, dict) or not since_raw:
        raise CheckError(f"surface table {path} 'since' must be a non-empty object of version groups")

    surfaces: dict[str, Surface] = {}
    for since_text, group in since_raw.items():
        since = parse_version(since_text, f"since group in {path}")
        if since <= minimum:
            raise CheckError(
                f"since group {since_text} in {path} is at or below minimum_supported_floor — "
                "those surfaces belong in 'baseline'"
            )
        if not isinstance(group, dict) or not group:
            raise CheckError(f"since group {since_text} in {path} must be a non-empty object")
        for identifier, value in group.items():
            if not isinstance(identifier, str) or identifier != identifier.strip() or not identifier:
                raise CheckError(f"surface identifier {identifier!r} must be a single trimmed token")
            if identifier in surfaces:
                raise CheckError(
                    f"surface {identifier!r} appears in more than one since group in {path}"
                )
            negotiated = True
            if isinstance(value, str):
                declaration = value or identifier
            elif isinstance(value, dict):
                unknown = set(value) - {"declaration", "negotiated"}
                if unknown:
                    raise CheckError(
                        f"surface {identifier!r} in {path} has unknown keys: {', '.join(sorted(unknown))}"
                    )
                declaration_raw = value.get("declaration", "")
                if not isinstance(declaration_raw, str):
                    raise CheckError(f"surface {identifier!r} in {path} needs a string 'declaration'")
                declaration = declaration_raw or identifier
                negotiated = value.get("negotiated", True)
                if not isinstance(negotiated, bool):
                    raise CheckError(f"surface {identifier!r} in {path} needs a boolean 'negotiated'")
            else:
                raise CheckError(
                    f"surface {identifier!r} in {path} must map to a declaration string or an object"
                )
            surfaces[identifier] = Surface(
                identifier, since, negotiated, declaration, surface_matcher(identifier)
            )

    # Baseline surfaces predate (or equal) the minimum supported floor, so they
    # can never raise a floor at or above it — listed so verdicts can name them
    # and so an exception naming one is "known" (and rejected as unnecessary
    # via the stale-claim rule when nothing matches).
    baseline_raw = document.get("baseline", [])
    if not isinstance(baseline_raw, list):
        raise CheckError(f"surface table {path} baseline must be an array of identifiers")
    for identifier in baseline_raw:
        if not isinstance(identifier, str) or identifier != identifier.strip() or not identifier:
            raise CheckError(f"baseline identifier {identifier!r} must be a single trimmed token")
        if identifier in surfaces:
            raise CheckError(
                f"surface {identifier!r} in {path} is listed twice (surfaces/baseline overlap "
                "or duplicate baseline entry)"
            )
        surfaces[identifier] = Surface(
            identifier, minimum, True, identifier, surface_matcher(identifier)
        )
    return SurfaceTable(surfaces, frozenset(baseline_raw), minimum, path)


# --- C/C++ source scanning --------------------------------------------------


def _blank_except_newlines(text: str) -> str:
    return "".join("\n" if char == "\n" else " " for char in text)


def strip_cpp_comments(text: str) -> str:
    """Remove C/C++ comments while preserving offsets and string literals."""
    output: list[str] = []
    index = 0
    quote: str | None = None
    escaped = False
    while index < len(text):
        if quote is not None:
            char = text[index]
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            index += 1
            continue

        raw_match = RAW_STRING_START_RE.match(text, index)
        if raw_match:
            closing = ")" + raw_match.group(1) + '"'
            end = text.find(closing, raw_match.end())
            end = len(text) if end < 0 else end + len(closing)
            output.append(text[index:end])
            index = end
            continue
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = len(text) if end < 0 else end
            output.append(_blank_except_newlines(text[index:end]))
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = len(text) if end < 0 else end + 2
            output.append(_blank_except_newlines(text[index:end]))
            index = end
            continue
        char = text[index]
        if char in {'"', "'"}:
            quote = char
        output.append(char)
        index += 1
    return "".join(output)


def _splice_cpp_lines(text: str) -> tuple[str, list[int]]:
    """Apply phase-2 backslash-newline removal, retaining an offset mapping."""
    output: list[str] = []
    original_offsets: list[int] = []
    index = 0
    while index < len(text):
        if text.startswith("\\\r\n", index):
            index += 3
            continue
        if text.startswith("\\\n", index):
            index += 2
            continue
        output.append(text[index])
        original_offsets.append(index)
        index += 1
    return "".join(output), original_offsets


def _original_line(text: str, original_offsets: Sequence[int], spliced_offset: int) -> int:
    original_offset = (
        original_offsets[spliced_offset] if spliced_offset < len(original_offsets) else len(text)
    )
    return text.count("\n", 0, original_offset) + 1


def scan_source_file(path: Path, table: SurfaceTable) -> list[tuple[str, int]]:
    """All (surface identifier, 1-based line) matches in one source file."""
    try:
        raw_source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        raise CheckError(f"cannot read C/C++ source {path}: {exc}") from exc
    spliced_source, original_offsets = _splice_cpp_lines(raw_source)
    searchable_source = strip_cpp_comments(spliced_source)
    matches: list[tuple[str, int]] = []
    for surface in table.surfaces.values():
        # Fast native substring scan first: absence of the literal identifier
        # proves the boundary-aware regex cannot match (measured ~55x).
        if surface.identifier not in searchable_source:
            continue
        for match in surface.matcher.finditer(searchable_source):
            matches.append(
                (surface.identifier, _original_line(raw_source, original_offsets, match.start()))
            )
    return matches


def iter_source_files(directory: Path, excluded_dirs: frozenset[str] = frozenset()) -> Iterable[Path]:
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        relative_parts = path.relative_to(directory).parts[:-1]
        if any(part in excluded_dirs or part.startswith("build-") for part in relative_parts):
            continue
        yield path


# --- Exception + floor validation -------------------------------------------


def validate_manifest_shape(manifest: dict) -> list[str]:
    """Table-free shape rules for the floor-contract manifest fields.

    ``suggested_sdk_version`` is the FULL-FEATURE floor — the SDK release at
    which every capability the plugin uses is available — while
    ``min_sdk_required`` stays the functioning floor. ``floor_test`` names the
    ONE gtest proving the plugin functions against a floor-level host
    (degradations active). The two travel together: declaring a degraded range
    without its proof, or a proof without the range, is malformed. Whether the
    values are CORRECT against real surface usage is the table-aware check.
    """
    errors: list[str] = []
    if manifest.get("sdk_floor_exceptions") is not None:
        errors.append(
            "sdk_floor_exceptions is no longer supported — declare "
            "suggested_sdk_version + floor_test instead"
        )
    suggested = manifest.get("suggested_sdk_version")
    floor_test = manifest.get("floor_test")
    if suggested is not None:
        if not isinstance(suggested, str) or not VERSION_RE.fullmatch(suggested):
            errors.append(
                f"suggested_sdk_version must name a stable release as X.Y.Z, got {suggested!r}"
            )
        else:
            minimum = manifest.get("min_sdk_required")
            if isinstance(minimum, str) and VERSION_RE.fullmatch(minimum):
                if tuple(map(int, suggested.split("."))) < tuple(map(int, minimum.split("."))):
                    errors.append(
                        f"suggested_sdk_version {suggested} is below min_sdk_required {minimum}"
                    )
        if floor_test is None:
            errors.append(
                "floor_test is required alongside suggested_sdk_version: name the gtest "
                "proving the plugin functions against a floor-level host"
            )
    if floor_test is not None:
        if not isinstance(floor_test, str) or "." not in floor_test:
            errors.append("floor_test must name a gtest as 'Suite.Name'")
        if suggested is None:
            errors.append("floor_test is only meaningful alongside suggested_sdk_version — remove it")
    return errors


def test_exists(test_dirs: Iterable[Path], test_name: str) -> bool:
    """True when `Suite.Name` is a real gtest TEST/TEST_F/TEST_P declaration.

    Sources are comment-stripped first: a commented-out test must not satisfy
    the floor_test obligation.
    """
    if "." not in test_name:
        return False
    suite, _, name = test_name.partition(".")
    pattern = re.compile(
        r"TEST(?:_F|_P)?\s*\(\s*" + re.escape(suite) + r"\s*,\s*" + re.escape(name) + r"\s*[,)]"
    )
    for directory in test_dirs:
        if not directory.is_dir():
            continue
        for path in iter_source_files(directory):
            try:
                text = path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                continue
            if pattern.search(strip_cpp_comments(text)):
                return True
    return False


def floor_problems(
    manifest: dict,
    declared: tuple[int, int, int],
    table: SurfaceTable,
    matched: dict[str, str],
    test_dirs: Iterable[Path],
) -> list[str]:
    """The whole floor contract, table-aware.

    1. ``min_sdk_required`` >= since of every matched NON-negotiated surface —
       a hard surface above the floor is always a failure, nothing waives it.
    2. ``suggested_sdk_version`` is REQUIRED exactly when a matched surface's
       since exceeds the floor, and must equal max(since) over ALL matched
       surfaces; it is forbidden otherwise (it would repeat the floor).
    3. ``floor_test`` (required with suggested) must name a gtest that exists
       in the plugin's tests — the proof the degraded configuration works.
    """
    problems = validate_manifest_shape(manifest)
    if problems:
        return problems
    if declared < table.minimum_supported_floor:
        problems.append(
            f"min_sdk_required {format_version(declared)} is below the supported floor "
            f"{table.minimum_text} (older history is not classified)"
        )

    above_floor = sorted(
        (table.surfaces[identifier] for identifier in matched if table.surfaces[identifier].since > declared),
        key=lambda surface: (surface.since, surface.identifier),
    )
    for surface in above_floor:
        if not surface.negotiated:
            problems.append(
                f"{surface.identifier} (since {surface.since_text}) exceeds floor "
                f"{format_version(declared)} at {matched[surface.identifier]} and is not "
                "runtime-negotiated — raise min_sdk_required"
            )

    suggested = manifest.get("suggested_sdk_version")
    if above_floor:
        expected = format_version(
            max(table.surfaces[identifier].since for identifier in matched)
        )
        if suggested is None:
            newest = above_floor[-1]
            problems.append(
                f"suggested_sdk_version is missing: {newest.identifier} "
                f"(since {newest.since_text}, at {matched[newest.identifier]}) exceeds floor "
                f"{format_version(declared)} — declare suggested_sdk_version {expected} "
                "(+ floor_test) or raise min_sdk_required"
            )
        elif suggested != expected:
            problems.append(
                f"suggested_sdk_version is {suggested} but the full-feature floor over the "
                f"matched surfaces is {expected} — declare exactly that"
            )
        else:
            floor_test = manifest["floor_test"]
            if not test_exists(test_dirs, floor_test):
                problems.append(
                    f"floor_test {floor_test!r} not found in the plugin's tests"
                )
    elif suggested is not None:
        problems.append(
            "suggested_sdk_version is only meaningful when a used surface exceeds "
            "min_sdk_required (it would just repeat the floor) — remove it"
        )
    return problems


def effective_maximum(table: SurfaceTable, matched: dict[str, str]) -> tuple[int, int, int] | None:
    """The highest introducing release among matched surfaces (for over-declared warnings)."""
    return max((table.surfaces[identifier].since for identifier in matched), default=None)


# --- Standalone CLI for out-of-tree plugin authors ---------------------------


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check one plugin's min_sdk_required against the host surfaces its sources use."
    )
    parser.add_argument("--table", type=Path, required=True, help="Path to feature_floors.json")
    parser.add_argument("--manifest", type=Path, required=True, help="Path to the plugin's manifest.json")
    parser.add_argument(
        "--sources",
        type=Path,
        action="append",
        required=True,
        help="Source directory to scan (repeatable)",
    )
    parser.add_argument(
        "--tests",
        type=Path,
        action="append",
        help="Test directory for fallback-test lookup (default: <manifest dir>/tests and /test)",
    )
    args = parser.parse_args(argv)

    try:
        table = load_surface_table(args.table)
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        if not isinstance(manifest, dict):
            raise CheckError(f"{args.manifest} must contain a JSON object")
        declared = parse_version(manifest.get("min_sdk_required"), "min_sdk_required")

        matched: dict[str, str] = {}
        for directory in args.sources:
            for path in iter_source_files(directory, frozenset({"build", "test", "tests"})):
                for identifier, line in scan_source_file(path, table):
                    matched.setdefault(identifier, f"{path}:{line}")

        test_dirs = args.tests or [args.manifest.parent / "tests", args.manifest.parent / "test"]
        problems = floor_problems(manifest, declared, table, matched, test_dirs)
    except CheckError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if problems:
        print(f"FAIL (floor {format_version(declared)}):", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    summary = ", ".join(sorted(matched)) if matched else "no host surfaces matched"
    suggested = manifest.get("suggested_sdk_version")
    degraded_note = f", full features at {suggested}" if suggested else ""
    print(f"PASS (floor {format_version(declared)}{degraded_note}): {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
