#!/usr/bin/env python3
# Copyright 2026 Davide Faconti
# SPDX-License-Identifier: Apache-2.0
"""Inventory the SDK's HOST-CONTRACT surfaces from the protocol headers.

The inventory is what feature_floors.json must classify: every surface a
plugin can require from the HOST at runtime. Three kinds are extracted:

  1. Callable members of host-side vtables (top-level function-pointer
     members, including typedef'd callable types) — emitted as
     "Struct::member". Callback parameters nested inside a member's
     parameter list are NOT members and are skipped.
  2. Versioned host-served service ids ("pj.<name>.v<N>"). Plugin-provided
     extension ids are excluded: the HOST queries those from the plugin, so
     an older host simply never asking for a new one degrades by
     construction. Every discovered id must be classified in one of the two
     lists below — an unknown id fails the extraction.
  3. PJ_*_FLAG* macros in the protocol headers (wire-contract flags a host
     must understand).

Plugin-side vtables (PJ_data_source_vtable_t, ...) are deliberately absent:
the HOST calls those, so an old host not calling a new plugin slot degrades
by construction and never constrains a plugin's min_sdk_required.
Client-side static-library helpers are equally absent — they travel inside
the plugin binary. Every discovered vtable must be classified as host-side
or plugin-side below; an unknown one fails the extraction.

Run with --write to refresh host_surfaces.snapshot; CI diffs a fresh
extraction against the checked-in snapshot (see check_feature_floors.py).
"""

import argparse
import re
import sys
from pathlib import Path

# Host-side vtables beyond the PJ_*host_vtable_t naming convention.
EXTRA_HOST_VTABLES = {
    "PJ_service_registry_vtable_t",
    "PJ_colormap_registry_vtable_t",
    "PJ_settings_store_vtable_t",
}
# Plugin-implemented vtables: the host calls these, they never constrain floors.
PLUGIN_SIDE_VTABLES = {
    "PJ_data_source_vtable_t",
    "PJ_message_parser_vtable_t",
    "PJ_toolbox_vtable_t",
    "PJ_joinable_job_vtable_t",
}
# Versioned ids the HOST serves through the service registry (host surfaces).
HOST_SERVICE_IDS = {
    "pj.colormap.v1",
    "pj.data_processors.v1",
    "pj.parser_object_write.v1",
    "pj.parser_runtime.v1",
    "pj.parser_write.v1",
    "pj.playback.v1",
    "pj.plot_tabs.v1",
    "pj.runtime.v1",
    "pj.settings.v1",
    "pj.source_object_write.v1",
    "pj.source_promotion.v1",
    "pj.source_write.v1",
    "pj.toolbox_object_read.v1",
    "pj.toolbox_runtime.v1",
    "pj.toolbox_write.v1",
    "pj.viewport.v1",
}
# Versioned ids of PLUGIN-provided extensions (the host queries the plugin).
PLUGIN_EXTENSION_IDS = {
    "pj.descriptor_import.v1",
    "pj.parser_functional.v1",
    "pj.parser_functional.v2",
    "pj.parser_route_claims.v1",
    "pj.topic_subscription.v1",
}

VTABLE_TYPEDEF_RE = re.compile(r"typedef\s+struct\s+(\w+)?\s*\{")
FN_PTR_MEMBER_RE = re.compile(r"\(\s*\*\s*(?:const\s+)?(\w+)\s*\)\s*\(")
FN_PTR_TYPEDEF_RE = re.compile(r"typedef\s+[^;{]*?\(\s*\*\s*(\w+)\s*\)\s*\(")
TYPED_MEMBER_RE = re.compile(r"^\s*(?:const\s+)?(\w+)\s+(\w+)\s*$")
SERVICE_ID_RE = re.compile(r'"(pj\.[a-z_0-9.]+\.v\d+)"')
FLAG_RE = re.compile(r"#\s*define\s+(PJ_\w*_FLAG\w*)[\s(]")

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


def _struct_bodies(text: str, path: Path) -> list[tuple[str, str]]:
    """Every ``typedef struct [name] { body } name;`` as (name, body)."""
    results: list[tuple[str, str]] = []
    for match in VTABLE_TYPEDEF_RE.finditer(text):
        start = text.index("{", match.start())
        depth = 0
        for pos in range(start, len(text)):
            if text[pos] == "{":
                depth += 1
            elif text[pos] == "}":
                depth -= 1
                if depth == 0:
                    break
        else:
            raise SystemExit(f"unterminated struct in {path}")
        body = text[start + 1 : pos]
        name = match.group(1)
        if name is None:
            trailing = re.match(r"\s*(\w+)\s*;", text[pos + 1 :])
            if not trailing:
                continue
            name = trailing.group(1)
        results.append((name, body))
    return results


def _top_level_members(body: str) -> list[str]:
    """Member declarations split on ';' at parenthesis/brace depth zero."""
    members: list[str] = []
    depth = 0
    current: list[str] = []
    for char in body:
        if char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
        if char == ";" and depth == 0:
            members.append("".join(current).strip())
            current = []
        else:
            current.append(char)
    return [member for member in members if member]


def _callable_member_name(declaration: str, fn_ptr_typedefs: set[str]) -> str | None:
    fn_ptr = FN_PTR_MEMBER_RE.search(declaration)
    if fn_ptr:
        return fn_ptr.group(1)
    typed = TYPED_MEMBER_RE.match(declaration)
    if typed and typed.group(1) in fn_ptr_typedefs:
        return typed.group(2)
    return None


def extract(repo: Path) -> list[str]:
    surfaces: set[str] = set()
    header_texts: list[tuple[Path, str]] = []
    for rel, glob in PROTOCOL_HEADER_GLOBS:
        for header in sorted((repo / rel).glob(glob)):
            header_texts.append((header, strip_comments(header.read_text())))

    fn_ptr_typedefs: set[str] = set()
    for _, text in header_texts:
        fn_ptr_typedefs.update(FN_PTR_TYPEDEF_RE.findall(text))

    for path, text in header_texts:
        surfaces.update(FLAG_RE.findall(text))
        for struct_name, body in _struct_bodies(text, path):
            if not struct_name.endswith("_vtable_t"):
                continue
            is_host = struct_name in EXTRA_HOST_VTABLES or re.fullmatch(
                r"PJ_\w*host_vtable_t", struct_name
            )
            if not is_host:
                if struct_name not in PLUGIN_SIDE_VTABLES:
                    raise SystemExit(
                        f"unclassified vtable {struct_name} in {path}: add it to "
                        "EXTRA_HOST_VTABLES or PLUGIN_SIDE_VTABLES in extract_host_surfaces.py"
                    )
                continue
            for declaration in _top_level_members(body):
                member = _callable_member_name(declaration, fn_ptr_typedefs)
                if member is not None:
                    surfaces.add(f"{struct_name}::{member}")

    for rel, glob in SERVICE_ID_GLOBS:
        for header in sorted((repo / rel).glob(glob)):
            for service_id in SERVICE_ID_RE.findall(strip_comments(header.read_text())):
                if service_id in PLUGIN_EXTENSION_IDS:
                    continue
                if service_id not in HOST_SERVICE_IDS:
                    raise SystemExit(
                        f"unclassified versioned id {service_id!r} in {header}: add it to "
                        "HOST_SERVICE_IDS or PLUGIN_EXTENSION_IDS in extract_host_surfaces.py"
                    )
                surfaces.add(service_id)
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
