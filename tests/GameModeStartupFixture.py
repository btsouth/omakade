"""Private compositor/audio transport for startup lifecycle tests."""

import fcntl
import json
import os
from pathlib import Path
import re
import sys


path = Path(os.environ["GM_FIXTURE"])
with open(str(path) + ".lock", "w") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    state = json.loads(path.read_text())
    tool, *args = sys.argv[1:]

    def workspace(selector):
        return {"id": 1 if selector == "1" else -1337,
                "name": selector.removeprefix("name:")}

    def client(address, pid, where, title, app):
        return {"address": address, "pid": pid, "workspace": workspace(where),
                "mapped": True, "fullscreen": 2, "monitor": 0, "title": title, "class": app}

    def clients():
        journal_path = Path(os.environ["XDG_STATE_HOME"]) / "omakade/game-mode.json"
        try:
            journal = json.loads(journal_path.read_text())
        except (OSError, ValueError):
            journal = {}
        # Offscreen Qt has no window server. Reflect its cold hide during the
        # journaled park operation; actual mapping/focus is checked in Hyprland too.
        if journal.get("phase") == "active":
            state["park_completed"] = False
        elif journal.get("phase") == "parked" and not journal.get("window_placed"):
            state["park_completed"] = True
        hidden = False
        if journal.get("temporary_window"):
            if journal.get("phase") == "active":
                state["park_completed"] = False
            elif journal.get("phase") == "parked":
                if not journal.get("window_placed"):
                    state["park_completed"] = True
                    hidden = True
                else:
                    # A consumed park followed by new placement intent is resume.
                    hidden = not state.get("park_completed", False)
        result = [client("0xaa", state["owner"], state["owner_workspace"], "Omakade", "Omakade"),
                  client("0xcc", state["owner"], "special:omakade", "Omakade Game Mode Placeholder", "Omakade"),
                  client("0xdd", int(os.environ["GM_DESKTOP_PID"]), "1", "Desktop", "fixture")]
        if hidden:
            result[0]["mapped"] = state.get("guide_library", False)
        if state.get("hide_fails"):
            result[0]["mapped"] = True
        if state.get("ending") and journal.get("temporary_window"):
            result[0]["mapped"] = False
        if journal.get("phase") == "parked" and (not journal.get("window_placed")
                or not state.get("park_completed", False)):
            result[1]["mapped"] = False
        if state.get("ending"):
            result[1]["mapped"] = False
        if state["game_open"]:
            result.append(client("0xbb", state["game"], "name:omakade", "Game", "fixture-game"))
        return result

    if tool == "pactl":
        if args == ["get-default-sink"]:
            print("fixture-sink")
        elif args == ["-f", "json", "list", "sinks"]:
            state["refreshes"] = state.get("refreshes", 0) + 1
            print('[{"name":"fixture-sink"}]')
        elif args == ["-f", "json", "list", "sink-inputs"]:
            print(json.dumps([{"index": 7, "mute": state["mute"], "properties": {
                "application.process.id": str(state["game"]), "object.serial": "7007"}}
                ] if state["game_open"] else []))
        elif args[:1] == ["set-sink-input-mute"]:
            state["mute"] = args[2] == "1"
        else:
            sys.exit(1)
    elif args[:2] == ["-j", "monitors"]:
        print(json.dumps([{"id": 0, "name": "HEADLESS-1", "description": "Fixture",
            "focused": True, "width": 1920, "height": 1080,
            "activeWorkspace": workspace(state["workspace"])}]))
    elif args == ["-j", "clients"]:
        print(json.dumps(clients()))
    elif args == ["-j", "activewindow"]:
        print(json.dumps(next((item for item in clients() if item["address"] == state["focus"]), {})))
    elif args == ["-j", "workspaces"]:
        print(json.dumps([{**workspace("name:omakade"), "monitor": "HEADLESS-1"}]))
    elif args[:1] == ["eval"]:
        script = args[1]
        if state.get("fail_resume") and 'workspace = "name:omakade"' in script and ('window.move' in script or 'dsp.focus' in script):
            print("error: fixture resume placement failed")
            sys.exit(1)
        for line in script.splitlines():
            if "window.swap" in line:
                state["owner_workspace"] = "1" if state["owner_workspace"] == "name:omakade" else "special:omakade"
            elif "window.move" in line and 'address:0xaa' in line:
                match = re.search(r'workspace = "([^"]+)"', line)
                if match:
                    state["owner_workspace"] = match.group(1)
                    if "follow = false" not in line:
                        state["workspace"] = match.group(1)
            elif "dsp.focus" in line:
                match = re.search(r'window = "address:([^"]+)"', line)
                if match:
                    state["focus"] = match.group(1)
                    target = next((item for item in clients() if item["address"] == state["focus"]), None)
                    if target:
                        state["workspace"] = "name:omakade" if target["workspace"]["name"] == "omakade" else target["workspace"]["name"]
                match = re.search(r'workspace = "([^"]+)"', line)
                if match:
                    state["workspace"] = match.group(1)
        print("ok")
    else:
        sys.exit(1)
    temporary = path.with_suffix(".next")
    temporary.write_text(json.dumps(state))
    temporary.replace(path)
