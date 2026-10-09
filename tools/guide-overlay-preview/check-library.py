#!/usr/bin/env python3
"""Run inside omabox: a frozen fullscreen game must stay off the library workspace."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
output = Path(sys.argv[2])
output.mkdir(parents=True, exist_ok=True)
assert os.environ.get("HYPRLAND_INSTANCE_SIGNATURE") not in (None, "", "omabox-guard")
assert os.environ.get("HOME", "").startswith("/home/sbx"), "Use omabox"
root = Path(__file__).resolve().parents[2]


def hypr(*args):
    return subprocess.check_output(["hyprctl", *args], text=True, timeout=3)


def wait_for(predicate):
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.03)
    raise AssertionError("Compositor state did not settle")


def client(pid):
    return next((c for c in json.loads(hypr("-j", "clients")) if c["pid"] == pid and c["mapped"]), None)


def native_mode(window, mode):
    hypr("eval", f'hl.dispatch(hl.dsp.window.fullscreen_state({{window="address:{window["address"]}",internal={mode},client={mode}}}))')


library = game = None
with tempfile.TemporaryFile() as log:
    try:
        hypr("dispatch", 'hl.dsp.focus({workspace="1"})')
        library = subprocess.Popen([binary, "--demo"], stdout=log, stderr=log)
        library_window = wait_for(lambda: client(library.pid))
        native_mode(library_window, 0)
        game = subprocess.Popen(["qml6", str(root / "tools/guide-overlay-preview/FakeGame.qml")], stdout=log, stderr=log)
        game_window = wait_for(lambda: client(game.pid))
        native_mode(game_window, 2)
        hypr("eval", f'hl.dispatch(hl.dsp.focus({{window="address:{game_window["address"]}"}}))')
        wait_for(lambda: client(game.pid)["fullscreen"] == 2)
        assert client(library.pid)["workspace"] == client(game.pid)["workspace"]
        game.send_signal(signal.SIGSTOP)
        # Production parkNow's outside-Game-Mode landing action, with a real stopped FakeGame.
        hypr("dispatch", 'hl.dsp.focus({workspace="empty"})')
        landing = json.loads(hypr("-j", "activeworkspace"))["id"]
        subprocess.run([binary, "--guide-library"], check=True, timeout=5, stdout=log, stderr=log)
        wait_for(lambda: json.loads(hypr("-j", "activewindow")).get("pid") == library.pid)
        time.sleep(0.2)  # include the existing delayed focus callback
        clients = json.loads(hypr("-j", "clients"))
        (output / "clients.json").write_text(json.dumps(clients, indent=2))
        active = json.loads(hypr("-j", "activewindow"))
        (output / "active.json").write_text(json.dumps(active, indent=2))
        subprocess.run(["grim", str(output / "library.png")], check=True, timeout=5)
        library_window, game_window = client(library.pid), client(game.pid)
        assert library_window["workspace"]["id"] == landing, "Library activation returned to the frozen game's workspace"
        assert game_window["workspace"]["id"] != landing and not game_window["visible"]
        assert (game_window["fullscreen"], game_window["fullscreenClient"]) == (2, 2), "Frozen game presentation changed"
        assert active["pid"] == library.pid
        print("PASS: library focused on landing workspace; frozen game hidden with fullscreen 2/2")
    finally:
        if game:
            game.send_signal(signal.SIGCONT)
            game.terminate()
            game.wait(timeout=5)
        if library:
            subprocess.run([binary, "--quit"], timeout=5, stdout=log, stderr=log)
            library.wait(timeout=5)
        log.seek(0)
        (output / "apps.log").write_bytes(log.read())
