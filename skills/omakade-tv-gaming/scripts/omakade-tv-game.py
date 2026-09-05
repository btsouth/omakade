#!/usr/bin/env python3
"""Launch a game in an already configured Hyprland TV session."""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys


def positive_int(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def read_state(*command):
    result = subprocess.run(command, check=True, capture_output=True, text=True, timeout=5)
    return json.loads(result.stdout)


def launch_plan(args):
    for tool in ("hyprctl", "pactl", "gamescope") + (("mangoapp",) if args.fps else ()):
        if not shutil.which(tool):
            raise ValueError(f"Required command not found: {tool}")

    monitors = read_state("hyprctl", "-j", "monitors")
    monitor = next((item for item in monitors if item["name"] == args.output), None)
    if monitor is None or monitor.get("disabled") or monitor.get("dpmsStatus") is False:
        raise ValueError("TV output is unavailable; start the local TV session first.")
    if monitor.get("activeWorkspace", {}).get("id") != args.workspace:
        raise ValueError("TV is not displaying the reserved workspace; check the local session.")

    sinks = read_state("pactl", "-f", "json", "list", "sinks")
    if not any(sink["name"] == args.sink for sink in sinks):
        raise ValueError("TV audio sink is unavailable; refusing to use the desktop audio sink.")

    width = args.width or int(monitor["width"])
    height = args.height or int(monitor["height"])
    refresh = args.refresh or round(float(monitor["refreshRate"]))
    if any(not math.isfinite(value) or value <= 0 for value in (width, height, refresh)):
        raise ValueError("TV reported an invalid display mode.")

    command = ["gamescope", "--backend", "wayland", "-W", str(width), "-H", str(height),
               "-w", str(width), "-h", str(height), "-r", str(refresh), "-o", str(refresh), "-f"]
    environment = {"PULSE_SINK": args.sink, "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS": "1"}
    if args.hdr:
        command.append("--hdr-enabled")
        environment["DXVK_HDR"] = "1"
    if args.fps:
        command.append("--mangoapp")
        environment["MANGOHUD_CONFIG"] = "fps_only,position=top-left,font_size=32,background_alpha=0.25"
    command += ["--", *args.game]
    return command, environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, help="active Hyprland TV connector")
    parser.add_argument("--workspace", type=positive_int, required=True, help="reserved TV workspace")
    parser.add_argument("--sink", required=True, help="TV sink name from pactl list sinks")
    parser.add_argument("--width", type=positive_int)
    parser.add_argument("--height", type=positive_int)
    parser.add_argument("--refresh", type=positive_int)
    parser.add_argument("--hdr", action="store_true")
    parser.add_argument("--fps", action="store_true")
    inspection = parser.add_mutually_exclusive_group()
    inspection.add_argument("--check", action="store_true", help="check preconditions without launching")
    inspection.add_argument("--dry-run", action="store_true", help="print the checked launch plan")
    parser.add_argument("game", nargs=argparse.REMAINDER, help="-- COMMAND [ARGUMENTS...]")
    args = parser.parse_args()
    if args.game[:1] == ["--"]:
        args.game = args.game[1:]
    if not args.game and not args.check:
        parser.error("a game command after -- is required")

    try:
        command, changes = launch_plan(args)
        if args.check:
            print("TV workspace and audio sink are available. Window placement is owned by the local session.")
        elif args.dry_run:
            print(json.dumps({"command": command, "environment": changes}, indent=2))
        else:
            environment = dict(os.environ)
            if args.fps:
                environment.pop("MANGOHUD_CONFIGFILE", None)
            environment.update(changes)
            os.execvpe(command[0], command, environment)
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f"Omakade TV launch: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
