#!/usr/bin/env python3
"""Exercise the launcher CLI with isolated stand-ins for desktop commands."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


LAUNCHER = Path(__file__).resolve().parents[1] / "skills/omakade-tv-gaming/scripts/omakade-tv-game.py"


class TvGameLauncherTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.environment = {
            **os.environ,
            "PATH": str(self.root),
            "TEST_ROOT": str(self.root),
            "PULSE_SINK": "desktop-headset",
        }
        stand_in = "#!" + sys.executable + "\n" + '''
import json, os, pathlib, sys
root = pathlib.Path(os.environ["TEST_ROOT"])
name = pathlib.Path(sys.argv[0]).name
if name == "hyprctl":
    assert sys.argv[1:] == ["-j", "monitors"]
    print((root / "monitors.json").read_text())
elif name == "pactl":
    assert sys.argv[1:] == ["-f", "json", "list", "sinks"]
    print((root / "sinks.json").read_text())
elif name == "gamescope":
    keys = ["PULSE_SINK", "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "DXVK_HDR", "MANGOHUD_CONFIG"]
    (root / "launched.json").write_text(json.dumps({
        "arguments": sys.argv[1:], "environment": {k: os.environ.get(k) for k in keys}}))
else:
    raise AssertionError("unexpected command execution")
'''
        for tool in ("hyprctl", "pactl", "gamescope", "mangoapp"):
            path = self.root / tool
            path.write_text(stand_in)
            path.chmod(0o700)
        self.monitor = {"name": "TV-EXAMPLE", "width": 3840, "height": 2160,
                        "refreshRate": 59.94, "activeWorkspace": {"id": 12}, "dpmsStatus": True}
        self.write_state()

    def write_state(self):
        (self.root / "monitors.json").write_text(json.dumps([self.monitor]))
        (self.root / "sinks.json").write_text(json.dumps([{"name": "tv-audio"}]))

    def invoke(self, *arguments):
        return subprocess.run(
            [sys.executable, str(LAUNCHER), "--output", "TV-EXAMPLE", "--workspace", "12",
             "--sink", "tv-audio", *arguments],
            env=self.environment, capture_output=True, text=True, timeout=10,
        )

    def assert_not_launched(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse((self.root / "launched.json").exists())

    def test_disabled_tv_never_starts_game(self):
        (self.root / "monitors.json").write_text("[]")
        result = self.invoke("--", "game")
        self.assert_not_launched(result)
        self.assertIn("TV output is unavailable", result.stderr)

    def test_dpms_off_and_disabled_output_never_start_game(self):
        for field, value in (("dpmsStatus", False), ("disabled", True)):
            with self.subTest(field=field):
                self.monitor[field] = value
                self.write_state()
                self.assert_not_launched(self.invoke("--", "game"))
                self.monitor.pop(field)

    def test_other_workspace_is_not_a_gaming_session(self):
        self.monitor["activeWorkspace"]["id"] = 3
        self.write_state()
        self.assert_not_launched(self.invoke("--", "game"))

    def test_missing_tv_sink_never_falls_back_to_headset(self):
        (self.root / "sinks.json").write_text('[{"name":"desktop-headset"}]')
        result = self.invoke("--", "game")
        self.assert_not_launched(result)
        self.assertIn("refusing to use the desktop audio sink", result.stderr)

    def test_missing_optional_dependency_is_actionable(self):
        (self.root / "mangoapp").unlink()
        result = self.invoke("--fps", "--", "game")
        self.assert_not_launched(result)
        self.assertIn("mangoapp", result.stderr)

    def test_check_and_dry_run_do_not_launch(self):
        self.assertEqual(self.invoke("--check").returncode, 0)
        result = self.invoke("--dry-run", "--", "game")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["environment"]["PULSE_SINK"], "tv-audio")
        self.assertFalse((self.root / "launched.json").exists())

    def test_launch_keeps_arguments_literal_and_routes_only_child_audio(self):
        game = ["/a game/launch", "argument with spaces", "$(touch unwanted)", ";", "--flag"]
        result = self.invoke("--hdr", "--fps", "--", *game)
        self.assertEqual(result.returncode, 0, result.stderr)
        launched = json.loads((self.root / "launched.json").read_text())
        arguments = launched["arguments"]
        self.assertEqual(arguments[arguments.index("--") + 1:], game)
        self.assertEqual(arguments[arguments.index("-W") + 1], "3840")
        self.assertEqual(arguments[arguments.index("-r") + 1], "60")
        self.assertIn("--hdr-enabled", arguments)
        self.assertIn("--mangoapp", arguments)
        self.assertEqual(launched["environment"]["PULSE_SINK"], "tv-audio")
        self.assertEqual(self.environment["PULSE_SINK"], "desktop-headset")

    def test_overrides_and_optional_features(self):
        result = self.invoke("--width", "2560", "--height", "1440", "--refresh", "120", "--", "game")
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads((self.root / "launched.json").read_text())["arguments"]
        self.assertEqual(args[args.index("-w") + 1], "2560")
        self.assertEqual(args[args.index("-h") + 1], "1440")
        self.assertEqual(args[args.index("-r") + 1], "120")
        self.assertNotIn("--hdr-enabled", args)
        self.assertNotIn("--mangoapp", args)

    def test_invalid_input_and_malformed_desktop_state(self):
        self.assert_not_launched(self.invoke("--width", "0", "--", "game"))
        (self.root / "monitors.json").write_text("not json")
        self.assert_not_launched(self.invoke("--", "game"))


if __name__ == "__main__":
    unittest.main()
