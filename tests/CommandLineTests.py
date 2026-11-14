#!/usr/bin/env python3
"""Run the shipped binary's print-only options with no display at all."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


BINARY = sys.argv.pop(1)
VERSION = sys.argv.pop(1)


class CommandLineTests(unittest.TestCase):
    def run_omakade(self, *arguments):
        with tempfile.TemporaryDirectory() as home:
            runtime = Path(home) / "run"
            runtime.mkdir(mode=0o700)
            environment = {
                key: value for key, value in os.environ.items()
                if key not in {"QT_QPA_PLATFORM", "WAYLAND_DISPLAY", "DISPLAY",
                               "HYPRLAND_INSTANCE_SIGNATURE"}
                and not key.startswith("XDG_")
            }
            environment.update({"HOME": home, "XDG_RUNTIME_DIR": str(runtime)})
            return subprocess.run([BINARY, *arguments], env=environment, capture_output=True,
                                  text=True, timeout=10, check=False)

    def test_help_prints_and_exits(self):
        for option in ("--help", "-h"):
            with self.subTest(option=option):
                result = self.run_omakade(option)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout.startswith("Usage: omakade"), result.stdout)
                for documented in ("--play <key>", "--quit", "--game-mode-toggle", "--version"):
                    self.assertIn(documented, result.stdout)

    def test_version_prints_and_exits(self):
        result = self.run_omakade("--version")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"omakade {VERSION}\n")


if __name__ == "__main__":
    unittest.main()
