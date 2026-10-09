"""Exercise real startup/IPC ownership without touching desktop services."""

import fcntl
import json
import os
import socket
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


BINARY = str(Path(sys.argv.pop(1)).resolve())


class GameModeStartupTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="omakade-gm-startup-")
        root = Path(self.directory.name)
        self.state = root / "state/omakade/game-mode.json"
        self.env = os.environ.copy()
        self.env.update({
            "HOME": str(root),
            "TMPDIR": str(root),
            "XDG_CONFIG_HOME": str(root / "config"),
            "XDG_DATA_HOME": str(root / "data"),
            "XDG_STATE_HOME": str(root / "state"),
            "XDG_CACHE_HOME": str(root / "cache"),
            "XDG_RUNTIME_DIR": str(root / "runtime"),
            "QT_QPA_PLATFORM": "offscreen",
            "QT_QPA_PLATFORMTHEME": "",
            "QT_STYLE_OVERRIDE": "Fusion",
            "QT_QUICK_BACKEND": "software",
            "QT_FORCE_STDERR_LOGGING": "1",
            "HYPRLAND_INSTANCE_SIGNATURE": "",
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=" + str(root / "no-bus"),
            "NO_AT_BRIDGE": "1",
            "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT": "0xffff/0xffff",
        })
        (root / "runtime").mkdir(mode=0o700)
        tools = root / "tools"
        tools.mkdir()
        for name in ("hyprctl", "pactl", "omarchy-shell", "systemctl"):
            stub = tools / name
            stub.write_text("#!/bin/sh\nexit 1\n")
            stub.chmod(0o755)
        self.env["PATH"] = str(tools) + os.pathsep + self.env["PATH"]
        config = root / "config/omakade"
        config.mkdir(parents=True)
        (config / "game-mode.json").write_text(json.dumps({"silence_notifications": False}))
        self.log = open(root / "app.log", "w+")
        self.primary = None
        self.game = None
        # Keep real resident IPC in these legacy fallback ownership tests. Its
        # compositor is unavailable: the GUI transport below changes per test,
        # and intentionally requires variables absent at login.
        resident_tools = root / "resident-tools"
        resident_tools.mkdir()
        hyprctl = resident_tools / "hyprctl"
        hyprctl.write_text("#!/bin/sh\ncase \"$2\" in\nactivewindow) echo '{}';;\nclients|monitors) echo '[]';;\n*) exit 1;;\nesac\n")
        hyprctl.chmod(0o755)
        resident_env = self.env.copy()
        resident_env["PATH"] = str(resident_tools) + os.pathsep + self.env["PATH"]
        self.resident = subprocess.Popen(
            [str(Path(BINARY).with_name("omakade-sessiond")), "--guide-only"],
            env=resident_env, stdout=self.log, stderr=subprocess.STDOUT,
        )
        endpoint = str(root / "runtime" / f"omakade-guide-control-{os.getuid()}")
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            try:
                with socket.socket(socket.AF_UNIX) as channel:
                    channel.settimeout(0.5)
                    channel.connect(endpoint)
                    channel.sendall(b'{"action":"status"}\n')
                    status = json.loads(channel.recv(65536))
                    # Socket/protocol readiness is sufficient for this fallback
                    # fixture. An unavailable compositor correctly reports ready=false.
                    if status.get("result") == "handled" and "ready" in status:
                        break
            except (OSError, ValueError):
                pass
            if self.resident.poll() is not None:
                self.fail("Resident guide exited during fixture startup")
            time.sleep(0.02)
        else:
            self.fail("Resident guide did not prepare the fallback fixture")

    def tearDown(self):
        if self.primary is not None and self.primary.poll() is None:
            self.primary.terminate()
            try:
                self.primary.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.primary.kill()
                self.primary.wait(timeout=5)
        if self.game is not None and self.game.poll() is None:
            self.game.terminate()
            self.game.wait(timeout=5)
        self.resident.terminate()
        self.resident.wait(timeout=5)
        self.log.close()
        self.directory.cleanup()

    def wait_for(self, predicate, message):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if predicate():
                return
            if self.primary.poll() is not None:
                break
            time.sleep(0.02)
        self.log.flush()
        self.log.seek(0)
        details = self.log.read()[-4000:]
        for name in ("state", "fixture"):
            path = getattr(self, name, None)
            if path and path.exists():
                details += "\n" + name + ": " + path.read_text()
        self.fail(message + "\n" + details)

    def launch(self, *arguments):
        self.primary = subprocess.Popen(
            [BINARY, "--demo", *arguments], env=self.env,
            stdout=self.log, stderr=subprocess.STDOUT,
        )

    def command(self, argument):
        if argument in ("--game-mode-exit", "--quit") and getattr(self, "fixture", None):
            # Offscreen has no native unmap events. Model the requested end's
            # window-server side; boxed tests verify the actual Qt unmap.
            self.fixture_update(ending=True)
        result = subprocess.run([BINARY, argument], env=self.env,
                                capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stderr)

    def entered(self):
        def owned():
            try:
                return json.loads(self.state.read_text())["owner_pid"] == self.primary.pid
            except (OSError, ValueError, KeyError):
                return False
        self.wait_for(owned, "Game Mode did not start in the primary instance")

    def assert_temporary_launch_closes(self, start, leave):
        self.launch(start)
        self.entered()
        self.command(leave)
        try:
            code = self.primary.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.fail("Leaving Game Mode kept its temporary Omakade instance open")
        self.assertEqual(code, 0)
        self.assertFalse(self.state.exists(), "Desktop recovery state was not cleared")

    def retained_fixture(self, with_game=True):
        # Real process/start identities through the production procfs adapter;
        # only compositor/audio transport is fake. No desktop services are used.
        if with_game:
            self.game = subprocess.Popen(["sleep", "120"], env=self.env)
        root = Path(self.directory.name)
        self.fixture = root / "desktop.json"
        self.fixture.write_text(json.dumps({"owner": 0, "game": self.game.pid if self.game else 0,
            "workspace": "1", "owner_workspace": "1", "focus": "0xdd",
            "game_open": with_game, "mute": False}))
        self.env["GM_FIXTURE"] = str(self.fixture)
        self.env["GM_DESKTOP_PID"] = str(os.getpid())
        self.env["HYPRLAND_INSTANCE_SIGNATURE"] = "startup-fixture"
        source = Path(__file__).with_name("GameModeStartupFixture.py")
        for name in ("hyprctl", "pactl"):
            stub = root / "tools" / name
            stub.write_text("#!/bin/sh\nexec " + sys.executable + " " + str(source) + " " + name + ' "$@"\n')
        self.env["PATH"] = str(root / "tools") + os.pathsep + self.env["PATH"]

    def fixture_update(self, **changes):
        with open(str(self.fixture) + ".lock", "w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            value = json.loads(self.fixture.read_text())
            value.update(changes)
            temporary = self.fixture.with_suffix(".next")
            temporary.write_text(json.dumps(value))
            temporary.replace(self.fixture)

    def phase(self, expected):
        def matches():
            try:
                state = json.loads(self.state.read_text())
                return state["phase"] == expected and state["owner_pid"] == self.primary.pid
            except (OSError, ValueError, KeyError):
                return False
        self.wait_for(matches, "Retained session did not reach " + expected)

    def assert_retained_cycle(self, warm):
        self.retained_fixture()
        self.launch(*([] if warm else ["--game-mode-toggle"]))
        self.fixture_update(owner=self.primary.pid)
        if warm:
            socket = Path(self.env["TMPDIR"]) / f"omakade-{os.getuid()}"
            self.wait_for(socket.exists, "Primary did not claim IPC")
            self.command("--game-mode-toggle")
        self.phase("active")
        # The journal is written before effects. The post-change device refresh
        # witnesses completion of the asynchronous GUI/controller handoff.
        self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0) >= 2,
                      "Initial Game Mode handoff did not settle")
        owner = self.primary.pid
        for _ in range(2):
            refreshes = json.loads(self.fixture.read_text()).get("refreshes", 0)
            self.command("--game-mode-toggle")
            self.phase("parked")
            self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0) > refreshes,
                          "Return to Desktop handoff did not settle")
            self.wait_for(lambda: json.loads(self.fixture.read_text())["mute"],
                          "Game audio was not muted before returning")
            self.wait_for(lambda: json.loads(self.fixture.read_text())["focus"] == "0xdd",
                          "Desktop focus did not return")
            value = json.loads(self.fixture.read_text())
            self.assertTrue(value["mute"], "Game audio stayed audible")
            self.assertEqual(value["workspace"], "1")
            self.assertEqual(value["focus"], "0xdd")
            self.assertIsNone(self.primary.poll(), "Park destroyed the IPC owner")
            self.command("--game-mode-toggle")
            self.phase("active")
            self.wait_for(lambda: json.loads(self.fixture.read_text())["focus"] == "0xbb",
                          "Resume focused the library instead of the game")
            self.assertFalse(json.loads(self.fixture.read_text())["mute"])
            self.assertEqual(self.primary.pid, owner)
        refreshes = json.loads(self.fixture.read_text()).get("refreshes", 0)
        self.command("--game-mode-desktop")
        self.phase("parked")
        self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0) > refreshes
                      and json.loads(self.fixture.read_text())["focus"] == "0xdd"
                      and json.loads(self.fixture.read_text())["mute"],
                      "Final Return to Desktop handoff did not settle")
        self.game.terminate()
        self.game.wait(timeout=5)
        self.fixture_update(game_open=False)
        self.wait_for(lambda: not json.loads(self.state.read_text())["games"]
                      and not json.loads(self.state.read_text())["streams"],
                      "Ended game/audio records were not released")
        self.phase("parked")
        self.assertIsNone(self.primary.poll(), "Game exit destroyed the library owner")
        self.command("--game-mode-toggle")
        self.phase("active")
        self.wait_for(lambda: json.loads(self.fixture.read_text())["focus"] == "0xaa",
                      "Library-only resume did not focus Omakade")
        self.command("--game-mode-exit")
        self.wait_for(lambda: not self.state.exists(), "Explicit end kept the journal")
        if warm:
            self.assertIsNone(self.primary.poll(), "Ending Game Mode closed warm Omakade")
            self.command("--quit")
        self.assertEqual(self.primary.wait(timeout=8), 0)

    def test_retained_cold_session_resumes_same_game_and_owner(self):
        self.assert_retained_cycle(False)

    def test_retained_warm_session_keeps_existing_owner(self):
        self.assert_retained_cycle(True)

    def test_empty_park_discovers_delayed_game_and_resumes_same_owner(self):
        self.retained_fixture(with_game=False)
        self.launch("--game-mode-toggle")
        self.fixture_update(owner=self.primary.pid)
        self.phase("active")
        self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0) >= 2,
                      "Empty session entry handoff did not settle")
        self.command("--game-mode-desktop")
        self.phase("parked")
        self.wait_for(lambda: json.loads(self.fixture.read_text())["focus"] == "0xdd",
                      "Empty park did not restore desktop focus")
        self.game = subprocess.Popen(["sleep", "120"], env=self.env)
        self.fixture_update(game=self.game.pid, game_open=True)
        self.wait_for(lambda: json.loads(self.fixture.read_text())["mute"],
                      "Delayed game audio was not muted by parked polling")
        value = json.loads(self.fixture.read_text())
        self.assertEqual(value["focus"], "0xdd", "Parked discovery changed focus")
        self.assertEqual(value["workspace"], "1")
        self.assertEqual(json.loads(self.state.read_text())["games"][0]["pid"], self.game.pid)
        self.command("--game-mode-toggle")
        self.phase("active")
        self.wait_for(lambda: json.loads(self.fixture.read_text())["focus"] == "0xbb"
                      and not json.loads(self.fixture.read_text())["mute"],
                      "Resume did not restore the same delayed game and audio")
        self.assertIsNone(self.primary.poll(), "Delayed game changed the session owner")
        self.command("--game-mode-exit")
        self.assertEqual(self.primary.wait(timeout=8), 0)
        self.assertFalse(self.state.exists(), "Explicit end kept delayed-game recovery state")

    def test_desktop_action_without_an_owner_does_not_launch(self):
        result = subprocess.run([BINARY, "--game-mode-desktop"], env=self.env,
                                capture_output=True, text=True, timeout=8)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.state.exists())

    def test_unsupported_compositor_refuses_return_and_explicit_end_closes(self):
        # Keep audio's read-only refresh counter as a GUI handoff witness, but
        # leave the compositor unavailable. Journal ownership is entry intent.
        self.retained_fixture(with_game=False)
        self.env["HYPRLAND_INSTANCE_SIGNATURE"] = ""
        self.launch("--game-mode-toggle")
        self.entered()
        self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0) >= 2,
                      "Unsupported compositor entry handoff did not settle")
        self.command("--game-mode-toggle")
        self.wait_for(lambda: "Quiet Return to Desktop" in Path(self.log.name).read_text(),
                      "Unavailable compositor did not explain refused park")
        self.phase("active")
        self.assertIsNone(self.primary.poll())
        self.command("--game-mode-exit")
        self.assertEqual(self.primary.wait(timeout=8), 0)
        self.assertFalse(self.state.exists())

    def test_explicit_game_mode_launch_closes_on_exit(self):
        self.assert_temporary_launch_closes("--game-mode", "--game-mode-exit")

    def assert_library_session_retained(self, cold, couch=False, close=False):
        self.retained_fixture(with_game=False)
        self.launch(*(["--game-mode-toggle"] if cold else ["--couch"] if couch else []))
        self.fixture_update(owner=self.primary.pid)
        if not cold:
            socket = Path(self.env["TMPDIR"]) / f"omakade-{os.getuid()}"
            self.wait_for(socket.exists, "Primary did not claim IPC")
            self.command("--game-mode-toggle")
        self.phase("active")
        self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0) >= 2,
                      "Initial library session handoff did not settle")
        for _ in range(3):
            refreshes = json.loads(self.fixture.read_text()).get("refreshes", 0)
            self.command("--game-mode-toggle")
            self.phase("parked")
            self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0) > refreshes
                          and json.loads(self.fixture.read_text())["focus"] == "0xdd",
                          "Library-only Return to Desktop did not settle")
            self.assertIsNone(self.primary.poll(), "Park destroyed the library owner")
            self.assertFalse(json.loads(self.state.read_text())["games"])
            # Reopening Omakade also resumes the retained root instead of making a new owner.
            resume_refreshes = json.loads(self.fixture.read_text()).get("refreshes", 0)
            self.command("--game-mode-toggle" if not couch else "--couch")
            self.phase("active")
            self.wait_for(lambda: json.loads(self.fixture.read_text())["focus"] == "0xaa",
                          "Resume did not focus the library")
            # Journal/focus writes precede the GUI handoff. Observe this resume's
            # final refresh before beginning the next park, so the old refresh
            # cannot be mistaken for completion of that next operation.
            self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0)
                          > resume_refreshes, "Library resume handoff did not settle")
        refreshes = json.loads(self.fixture.read_text()).get("refreshes", 0)
        self.command("--game-mode-desktop")
        self.phase("parked")
        self.wait_for(lambda: json.loads(self.fixture.read_text()).get("refreshes", 0) > refreshes
                      and json.loads(self.fixture.read_text())["focus"] == "0xdd",
                      "Final library park did not settle before close")
        self.command("--quit" if close else "--game-mode-exit")
        if not cold and not close:
            self.wait_for(lambda: not self.state.exists(), "Explicit end did not clear the journal")
            self.assertIsNone(self.primary.poll(), "Explicit end closed warm Omakade")
            self.command("--quit")
        self.assertEqual(self.primary.wait(timeout=8), 0)
        self.assertFalse(self.state.exists(), "Closing left retained recovery state")

    def test_library_only_cold_session_retains_owner_until_explicit_end(self):
        self.assert_library_session_retained(cold=True)

    def test_library_only_warm_desktop_session_retained_until_explicit_end(self):
        self.assert_library_session_retained(cold=False)

    def test_library_only_warm_couch_reopens_same_owner_and_close_cleans_up(self):
        self.assert_library_session_retained(cold=False, couch=True, close=True)


if __name__ == "__main__":
    unittest.main()
