#!/usr/bin/env python3
# Copyright 2026 Davide Faconti
# SPDX-License-Identifier: Apache-2.0
"""A development pre-release (X.Y.Z-dev.N) is a valid declared version; floors stay X.Y.Z."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import feature_floor_check as ffc  # noqa: E402

TABLE = Path(__file__).resolve().parents[2] / "pj_base/feature_floors.json"


class DeclaredVersionTest(unittest.TestCase):
    def test_pre_release_keeps_numeric_core(self):
        self.assertEqual(ffc.parse_declared_version("0.37.0-dev.1", "v"), ((0, 37, 0), True))
        self.assertEqual(ffc.parse_declared_version("0.37.0", "v"), ((0, 37, 0), False))

    def test_rejects_malformed(self):
        for value in ("0.37", "v0.37.0", "0.37.0-", "0.37.0 ", None, 37):
            with self.assertRaises(ffc.CheckError, msg=repr(value)):
                ffc.parse_declared_version(value, "v")

    def test_floors_still_name_stable_releases(self):
        with self.assertRaises(ffc.CheckError):
            ffc.parse_version("0.37.0-dev.1", "v")

    def test_plugin_cli_accepts_pre_release_min_sdk_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({"min_sdk_required": "0.37.0-dev.1"}))
            (root / "src").mkdir()
            (root / "src/plugin.cpp").write_text("int main() { return 0; }\n")
            argv = ["--table", str(TABLE), "--manifest", str(manifest), "--sources", str(root / "src")]
            self.assertEqual(ffc.main(argv), 0)


if __name__ == "__main__":
    unittest.main()
