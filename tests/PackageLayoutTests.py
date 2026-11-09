#!/usr/bin/env python3
"""Installs the build into a scratch prefix and checks what the package ships."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CMAKE, BUILD, VERSION, LIBDIR = sys.argv[1:5]
del sys.argv[1:5]


class PackageLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.scratch = tempfile.TemporaryDirectory()
        cls.prefix = Path(cls.scratch.name) / "usr"
        subprocess.run([CMAKE, "--install", BUILD, "--prefix", str(cls.prefix)],
                       check=True, stdout=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.scratch.cleanup()

    def test_user_services_are_not_enabled_for_every_account(self) -> None:
        units = self.prefix / LIBDIR / "systemd" / "user"
        self.assertTrue((units / "omakade-sessiond.service").is_file())
        self.assertTrue((units / "omakade-guide-button.service").is_file())
        self.assertEqual([str(path) for path in units.glob("*.wants")], [])

    def test_plugin_manifest_matches_the_package(self) -> None:
        path = self.prefix / "share" / "omakade" / "omarchy-plugin" / "manifest.json"
        manifest = json.loads(path.read_text())
        self.assertEqual(manifest["id"], "omakade.guide")
        self.assertEqual(manifest["version"], VERSION)
        self.assertEqual(manifest["license"], "GPL-3.0-or-later")
        self.assertEqual(manifest["entryPoints"], {"overlay": "Guide.qml"})


if __name__ == "__main__":
    unittest.main()
