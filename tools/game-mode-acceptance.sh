#!/bin/bash
# Runs Game Mode against a real Hyprland and a real PipeWire, without touching the desktop
# it is started from. Everything happens inside an omabox: a nested Omarchy session with
# its own compositor, session bus and runtime directory.
#
#   tools/game-mode-acceptance.sh [path-to-omakade]
#
# The box gets a second, virtual display named TV-1 and a private PipeWire with two null
# sinks. What this cannot show is listed at the end of docs/GAME-MODE.md: a real panel
# accepting a mode, real HDMI audio, and a real game's own fullscreen behaviour.

set -u
BIN=${1:-./build/release/omakade}
[[ -x $BIN ]] || { echo "No Omakade binary at $BIN" >&2; exit 2; }
command -v omabox >/dev/null || { echo "omabox is required" >&2; exit 2; }

passed=0
failed=0
TV=TV-1
DESK_SINK=desk_speakers
TV_SINK=tv_hdmi

hc() { omabox hyprctl "$@"; }
box() { omabox run -- "$@" 2>/dev/null; }
lua() { hc eval "$1" >/dev/null; }

section() { printf '\n== %s\n' "$1"; }
expect() { # label actual expected
  if [[ $2 == "$3" ]]; then
    passed=$((passed + 1))
    printf '  ok    %s\n' "$1"
  else
    failed=$((failed + 1))
    printf '  FAIL  %s: got "%s", wanted "%s"\n' "$1" "$2" "$3"
  fi
}
until_true() { # seconds condition, evaluated again on every try
  local deadline=$((SECONDS + $1))
  shift
  while ((SECONDS <= deadline)); do
    eval "$*" && return 0
    sleep 0.3
  done
  return 1
}

window() { hc -j clients | jq -r --arg f "$1" '[.[] | select(.class | endswith("Omakade"))][0] | .[$f] // empty | tostring'; }
window_workspace() { hc -j clients | jq -r '[.[] | select(.class | endswith("Omakade"))][0].workspace.name // empty'; }
window_output() { hc -j monitors all | jq -r --argjson id "$(window monitor)" '.[] | select(.id == $id) | .name'; }
class_workspace() { hc -j clients | jq -r --arg c "$1" '[.[] | select(.class == $c)][0].workspace.name // empty'; }
class_address() { hc -j clients | jq -r --arg c "$1" '[.[] | select(.class == $c)][0].address // empty'; }
output_field() { hc -j monitors all | jq -r --arg n "$1" --arg f "$2" '.[] | select(.name == $n) | .[$f] | tostring'; }
output_workspace() { hc -j monitors all | jq -r --arg n "$1" '.[] | select(.name == $n) | .activeWorkspace.name'; }
workspace_exists() { hc -j workspaces | jq -e --arg n "$1" 'any(.[]; .name == $n)' >/dev/null; }
dnd() { box omarchy-shell notifications dndState; }
default_sink() { box pactl get-default-sink; }
stream_sink() { # sink name carrying the stream of a client binary
  local index
  index=$(box pactl -f json list sink-inputs | jq -r --arg b "$1" '[.[] | select(.properties["application.process.binary"] == $b)][0].sink // empty')
  [[ -n $index ]] && box pactl -f json list sinks | jq -r --argjson i "$index" '.[] | select(.index == $i) | .name'
}

app_running() { [[ -n $(window address) ]]; }
app_gone() { [[ -z $(window address) ]]; }
state_present() { [[ -f $STATE ]]; }
state_gone() { [[ ! -f $STATE ]]; }
in_game_mode() { state_present && [[ $(window fullscreen) == 2 ]]; }

configure() { # output-name sink
  mkdir -p "$HOME_DIR/.config/omakade"
  printf '{"output_name":"%s","output_description":"","sink":"%s","silence_notifications":true}\n' \
    "$1" "$2" >"$HOME_DIR/.config/omakade/game-mode.json"
}
start_app() {
  omabox run -d -- "$BIN" --demo "$@" >/dev/null 2>&1
  until_true 15 app_running
}
quit_app() {
  box "$BIN" --quit
  until_true 10 app_gone
}
enter() {
  box "$BIN" --game-mode
  until_true 20 in_game_mode
}
leave() {
  box "$BIN" --game-mode-exit
  until_true 15 state_gone
}
main_address() { hc -j clients | jq -r '[.[] | select(.class | endswith("Omakade")) | select(.title == "Omakade")][0].address // empty'; }
press() { omabox keys --window "$(main_address)" --wait "$@" >/dev/null 2>&1; }
# `window` drops false values, so a flag that may be false is read here.
idle_held() { hc -j clients | jq -r '[.[] | select(.class | endswith("Omakade")) | select(.title == "Omakade")][0].inhibitingIdle | tostring'; }
session_field() { jq -r --arg f "$1" '.[$f] | tostring' "$STATE" 2>/dev/null; }
remembered() { grep -s '^couch_mode_enabled' "$HOME_DIR/.config/omakade/config.toml" | sed 's/.*= *//'; }
remember() { # true|false
  local file=$HOME_DIR/.config/omakade/config.toml
  mkdir -p "${file%/*}"
  if grep -qs '^couch_mode_enabled' "$file"; then
    sed -i "s/^couch_mode_enabled = .*/couch_mode_enabled = $1/" "$file"
  else
    echo "couch_mode_enabled = $1" >>"$file"
  fi
}
reset() { # output-name sink [tv on|off]
  app_running && quit_app
  rm -f "$STATE"
  box pactl set-default-sink "$DESK_SINK"
  [[ $(dnd) == on ]] && box omarchy-shell notifications setDnd false >/dev/null
  if [[ ${3:-off} == on ]]; then
    lua "hl.monitor({ output = \"$TV\", disabled = false })"
  else
    lua "hl.monitor({ output = \"$TV\", disabled = true })"
  fi
  lua 'hl.dispatch(hl.dsp.focus({ workspace = "1" }))'
  configure "$1" "$2"
  # A fullscreen start would begin Game Mode on its own.
  remember false
}
expect_desktop_restored() {
  expect "Omakade is back on workspace 1" "$(window_workspace)" 1
  expect "Omakade is windowed again" "$(window fullscreen)" 0
  expect "notifications are unsilenced" "$(dnd)" off
  expect "sound is back on the desk output" "$(default_sink)" "$DESK_SINK"
  workspace_exists omakade
  expect "the Game Mode workspace is gone" $? 1
  state_present
  expect "no session record is left" $? 1
}

omabox up >/dev/null 2>&1 || { echo "Could not start a box" >&2; exit 2; }
trap 'omabox down >/dev/null 2>&1' EXIT
HOME_DIR=$(omabox path)/home
STATE=$HOME_DIR/.local/state/omakade/game-mode.json

# A private sound server. The box has its own runtime directory, so this never reaches
# the host's PipeWire.
for service in pipewire wireplumber pipewire-pulse; do
  omabox run -d -- "$service" >/dev/null 2>&1
done
until_true 15 box pactl info >/dev/null || { echo "PipeWire did not start in the box" >&2; exit 2; }
box pactl load-module module-null-sink sink_name=$DESK_SINK sink_properties=device.description=Desk >/dev/null
box pactl load-module module-null-sink sink_name=$TV_SINK sink_properties=device.description=TV >/dev/null
hc output create headless $TV >/dev/null
DESK=$(hc -j monitors | jq -r '[.[] | select(.name != "'$TV'")][0].name')

section "1. One display: start and leave"
reset "" ""
start_app
enter
expect "Game Mode started" $? 0
expect "Game Mode is fullscreen" "$(window fullscreen)" 2
expect "it is on its own workspace" "$(window_workspace)" omakade
expect "it stayed on the same display" "$(window_output)" "$DESK"
expect "notifications are silenced" "$(dnd)" on
expect "sound was left alone" "$(default_sink)" "$DESK_SINK"
leave
expect_desktop_restored

section "2. A second display that is already on"
reset $TV "" on
lua "hl.dispatch(hl.dsp.focus({ monitor = \"$TV\" })) hl.dispatch(hl.dsp.focus({ workspace = \"7\" })) hl.dispatch(hl.dsp.focus({ monitor = \"$DESK\" }))"
before=$(output_workspace $TV)
start_app
enter
expect "Game Mode started" $? 0
expect "Game Mode opened on the second display" "$(window_output)" $TV
leave
expect_desktop_restored
expect "the second display is still on" "$(output_field $TV disabled)" false
expect "it shows the workspace it had before" "$(output_workspace $TV)" "$before"
expect "focus returned to the first display" "$(output_field "$DESK" focused)" true

section "3. A display that is normally off"
reset $TV ""
start_app
expect "the display starts off" "$(output_field $TV disabled)" true
enter
expect "Game Mode started" $? 0
expect "the display was turned on" "$(output_field $TV disabled)" false
expect "Game Mode opened on it" "$(window_output)" $TV
leave
expect_desktop_restored
expect "the display is off again" "$(output_field $TV disabled)" true

section "4. Sound output"
reset "" $TV_SINK
start_app
omabox run -d -- pacat --playback --client-name=before /dev/zero >/dev/null 2>&1
until_true 10 '[[ -n $(stream_sink pacat) ]]'
expect "music playing beforehand is on the desk output" "$(stream_sink pacat)" "$DESK_SINK"
enter
expect "Game Mode started" $? 0
expect "the default output is the TV" "$(default_sink)" "$TV_SINK"
until_true 5 '[[ $(stream_sink pacat) == "$TV_SINK" ]]'
expect "a stream that follows the default moved to the TV" "$(stream_sink pacat)" "$TV_SINK"
leave
expect_desktop_restored
until_true 5 '[[ $(stream_sink pacat) == "$DESK_SINK" ]]'
expect "the stream moved back" "$(stream_sink pacat)" "$DESK_SINK"
box pkill -x pacat

section "4b. A sound output chosen during the session is kept"
reset "" $TV_SINK
box pactl load-module module-null-sink sink_name=other sink_properties=device.description=Other >/dev/null
start_app
enter
box pactl set-default-sink other
leave
expect "the chosen output was kept" "$(default_sink)" other
box pactl set-default-sink $DESK_SINK

section "4c. A sound output that is missing stops Game Mode from starting"
reset $TV missing_sink
start_app
box "$BIN" --game-mode
until_true 6 state_present
until_true 14 state_gone
state_present
expect "nothing is recorded as changed" $? 1
expect "the display was turned back off" "$(output_field $TV disabled)" true
expect "Omakade stayed windowed" "$(window fullscreen)" 0
expect "notifications were not silenced" "$(dnd)" off

section "5. Windows opened during Game Mode"
reset "" ""
start_app
# A launcher that was already open elsewhere and starts its game later.
omabox run -d -- foot --app-id fake-launcher sh -c 'sleep 6; exec foot --app-id fake-late-game' >/dev/null 2>&1
until_true 10 '[[ -n $(class_address fake-launcher) ]]'
enter
omabox run -d -- foot --app-id fake-game >/dev/null 2>&1
until_true 10 '[[ -n $(class_address fake-game) ]]'
expect "a game started now opens on the Game Mode workspace" "$(class_workspace fake-game)" omakade
until_true 12 '[[ -n $(class_address fake-late-game) ]]'
expect "a game from an already open launcher opens there too" "$(class_workspace fake-late-game)" omakade
expect "the launcher itself stayed where it was" "$(class_workspace fake-launcher)" 1
lua "hl.dispatch(hl.dsp.window.close({ window = \"address:$(class_address fake-game)\" }))"
lua "hl.dispatch(hl.dsp.window.close({ window = \"address:$(class_address fake-late-game)\" }))"
until_true 10 '[[ -z $(class_address fake-game)$(class_address fake-late-game) ]]'
until_true 5 '[[ $(hc -j activewindow | jq -r .class) == io.github.tsouth89.Omakade ]]'
expect "Omakade has focus again after the games close" "$(hc -j activewindow | jq -r '.class')" io.github.tsouth89.Omakade
until_true 5 '[[ $(window fullscreen) == 2 ]]'
expect "it is still fullscreen" "$(window fullscreen)" 2
leave
expect_desktop_restored
lua "hl.dispatch(hl.dsp.window.close({ window = \"address:$(class_address fake-launcher)\" }))"

section "6. Omakade is killed during Game Mode"
reset $TV $TV_SINK
start_app
enter
box kill -9 "$(window pid)"
until_true 10 app_gone
expect "the display is left on by the crash" "$(output_field $TV disabled)" false
expect "the session record survives the crash" "$([[ -f $STATE ]] && echo yes)" yes
start_app
until_true 15 state_gone
expect "the next start turned the display off" "$(output_field $TV disabled)" true
expect "and put sound back" "$(default_sink)" "$DESK_SINK"
expect "and unsilenced notifications" "$(dnd)" off

section "6b. The same, undone from the command line"
reset $TV $TV_SINK
start_app
enter
box kill -9 "$(window pid)"
until_true 10 app_gone
box "$BIN" --game-mode-exit
expect "the command reports success" $? 0
expect "the display is off" "$(output_field $TV disabled)" true
expect "sound is back" "$(default_sink)" "$DESK_SINK"
expect "notifications are unsilenced" "$(dnd)" off

section "6c. Closing Omakade during Game Mode"
reset $TV $TV_SINK
start_app
enter
quit_app
expect "the display is off" "$(output_field $TV disabled)" true
expect "sound is back" "$(default_sink)" "$DESK_SINK"
expect "notifications are unsilenced" "$(dnd)" off
state_present
expect "no session record is left" $? 1

section "6d. Closing Omakade's window during Game Mode"
reset $TV $TV_SINK
start_app
enter
lua "hl.dispatch(hl.dsp.window.close({ window = \"address:$(main_address)\" }))"
until_true 15 app_gone
expect "Omakade closed" "$(app_gone && echo yes)" yes
expect "the display is off" "$(output_field $TV disabled)" true
expect "sound is back" "$(default_sink)" "$DESK_SINK"
expect "notifications are unsilenced" "$(dnd)" off
expect "no session record is left" "$([[ -f $STATE ]] && echo left || echo none)" none

section "7. The display is unplugged during Game Mode"
reset $TV $TV_SINK
start_app
enter
hc output remove $TV >/dev/null
until_true 15 state_gone
expect "Game Mode ended" "$([[ -f $STATE ]] && echo still-on || echo ended)" ended
expect "sound is back" "$(default_sink)" "$DESK_SINK"
expect "notifications are unsilenced" "$(dnd)" off
expect "Omakade is still running" "$(app_running && echo yes)" yes
hc output create headless $TV >/dev/null

section "7b. The display is disabled during Game Mode"
reset $TV $TV_SINK
start_app
enter
lua "hl.monitor({ output = \"$TV\", disabled = true })"
until_true 15 state_gone
expect "Game Mode ended" "$([[ -f $STATE ]] && echo still-on || echo ended)" ended
expect "sound is back" "$(default_sink)" "$DESK_SINK"
expect "notifications are unsilenced" "$(dnd)" off

section "8. The display never turns on"
reset $TV $TV_SINK
# Swallow the one command that enables the output, as a television that is powered off
# or on another input would: the request is accepted and nothing appears.
mkdir -p "$HOME_DIR/.local/bin"
cat >"$HOME_DIR/.local/bin/hyprctl" <<'STUB'
#!/bin/sh
case "$*" in *'disabled = false'*) echo ok; exit 0 ;; esac
exec /usr/bin/hyprctl "$@"
STUB
chmod +x "$HOME_DIR/.local/bin/hyprctl"
start_app
started=$SECONDS
box "$BIN" --game-mode
until_true 6 state_present
expect "the attempt is recorded while it waits" "$([[ -f $STATE ]] && echo yes)" yes
until_true 20 state_gone
waited=$((SECONDS - started))
rm -f "$HOME_DIR/.local/bin/hyprctl"
expect "it gave up" "$([[ -f $STATE ]] && echo waiting || echo gave-up)" gave-up
expect "after about ten seconds" "$( ((waited >= 9 && waited <= 20)) && echo yes || echo "$waited s")" yes
expect "the display is off" "$(output_field $TV disabled)" true
expect "sound was never switched" "$(default_sink)" "$DESK_SINK"
expect "notifications were never silenced" "$(dnd)" off
expect "Omakade stayed windowed on workspace 1" "$(window_workspace)/$(window fullscreen)" 1/0
quit_app

section "9. F11 plays here and leaves a configured TV, sound and notifications alone"
reset $TV $TV_SINK
start_app
press F11
until_true 15 in_game_mode
expect "F11 started Game Mode" $? 0
expect "it is on its own workspace" "$(window_workspace)" omakade
expect "it stayed on the desk display" "$(window_output)" "$DESK"
expect "the session is recorded on the desk display" "$(session_field output)" "$DESK"
expect "the TV stayed off" "$(output_field $TV disabled)" true
expect "sound was left alone" "$(default_sink)" "$DESK_SINK"
expect "notifications were left alone" "$(dnd)" off
expect "the screen may sleep while no game runs" "$(idle_held)" false
expect "fullscreen is remembered for the next start" "$(remembered)" true
press F11
until_true 15 state_gone
expect "F11 with no game running ended Game Mode" $? 0
expect_desktop_restored
expect "the TV is still off" "$(output_field $TV disabled)" true
expect "leaving fullscreen is remembered" "$(remembered)" false

section "9b. Game Mode on the TV still keeps the screen awake"
reset $TV $TV_SINK
start_app
enter
expect "Game Mode started on the TV" "$(window_output)" $TV
expect "the screen is kept awake" "$(idle_held)" true
leave
expect_desktop_restored

section "10. Leaving with a game on the Game Mode workspace opens the controls"
reset "" ""
start_app
press F11
until_true 15 in_game_mode
omabox run -d -- foot --app-id fake-game >/dev/null 2>&1
until_true 10 '[[ -n $(class_address fake-game) ]]'
expect "the game opened on the Game Mode workspace" "$(class_workspace fake-game)" omakade
press F11
until_true 4 state_gone
expect "Game Mode stayed on" $? 1
expect "Omakade is still fullscreen" "$(window fullscreen)" 2
lua "hl.dispatch(hl.dsp.window.close({ window = \"address:$(class_address fake-game)\" }))"
until_true 10 '[[ -z $(class_address fake-game) ]]'
leave
expect_desktop_restored

section "11. --couch and a remembered fullscreen start play here"
reset $TV $TV_SINK
start_app --couch
until_true 15 in_game_mode
expect "--couch started Game Mode" $? 0
expect "on the desk display" "$(window_output)" "$DESK"
expect "the TV stayed off" "$(output_field $TV disabled)" true
expect "sound was left alone" "$(default_sink)" "$DESK_SINK"
expect "notifications were left alone" "$(dnd)" off
quit_app
expect "closing ended Game Mode" "$([[ -f $STATE ]] && echo still-on || echo ended)" ended
reset $TV $TV_SINK
remember true
start_app
until_true 15 in_game_mode
expect "a remembered fullscreen start began Game Mode" $? 0
expect "on the desk display" "$(window_output)" "$DESK"
expect "the TV stayed off" "$(output_field $TV disabled)" true
press F11
until_true 15 state_gone
expect "F11 ended it" $? 0
until_true 5 '[[ $(window fullscreen) == 0 ]]'
expect_desktop_restored
expect "leaving fullscreen is remembered" "$(remembered)" false
# Qt puts a fullscreen window on its first screen, the desk; the start must follow focus.
reset $DESK $DESK_SINK on
lua "hl.dispatch(hl.dsp.focus({ monitor = \"$TV\" }))"
remember true
start_app
until_true 15 in_game_mode
expect "a remembered start with the TV focused began Game Mode" $? 0
expect "on the focused display" "$(window_output)" "$TV"
expect "and the session is there too" "$(session_field output)" "$TV"
press F11
until_true 15 state_gone
expect "F11 ended it" $? 0
until_true 5 '[[ $(window fullscreen) == 0 ]]'
expect "Omakade is windowed again" "$(window fullscreen)" 0

section "11b. The controller Home button with no game plays here, as Start does"
# With no game running, the Home button service runs --fullscreen-toggle.
reset $TV $TV_SINK
omabox run -d -- "$BIN" --demo --fullscreen-toggle >/dev/null 2>&1
until_true 15 app_running
until_true 15 in_game_mode
expect "Home with Omakade closed started Game Mode" $? 0
expect "on the desk display" "$(window_output)" "$DESK"
expect "the TV stayed off" "$(output_field $TV disabled)" true
expect "sound was left alone" "$(default_sink)" "$DESK_SINK"
expect "notifications were left alone" "$(dnd)" off
box "$BIN" --fullscreen-toggle
until_true 15 state_gone
expect "the next Home ended it" $? 0
until_true 5 '[[ $(window fullscreen) == 0 ]]'
expect "Omakade stayed open, windowed" "$(window fullscreen)" 0
box "$BIN" --fullscreen-toggle
until_true 15 in_game_mode
expect "Home in the open window started Game Mode again" $? 0
expect "on the display Omakade is on" "$(window_output)" "$DESK"
box "$BIN" --fullscreen-toggle
until_true 15 state_gone
expect "and the next Home ended it" $? 0
expect "leaving is remembered" "$(remembered)" false
quit_app

section "12. A Sunshine stream plays here, and its --quit ends Game Mode"
reset $TV $TV_SINK
omabox run -d -- env SUNSHINE_APP_ID=1 "$BIN" --demo >/dev/null 2>&1
until_true 15 app_running
until_true 15 in_game_mode
expect "the stream started Game Mode" $? 0
expect "on the display it opened on" "$(window_output)" "$DESK"
expect "the stream's sound was left alone" "$(default_sink)" "$DESK_SINK"
expect "the TV stayed off" "$(output_field $TV disabled)" true
quit_app
expect "Omakade closed" "$(app_gone && echo yes)" yes
expect "no session record is left" "$([[ -f $STATE ]] && echo left || echo none)" none
expect "sound is still on the desk output" "$(default_sink)" "$DESK_SINK"

section "13. A start that cannot begin falls back to plain fullscreen"
reset "" ""
# Another live Omakade owns a session, so this one cannot start its own.
mkdir -p "$HOME_DIR/owner"
cp /usr/bin/sleep "$HOME_DIR/owner/omakade"
omabox run -d -- /home/sbx/owner/omakade 600 >/dev/null 2>&1
until_true 5 '[[ -n $(box pgrep -f "^/home/sbx/owner/omakade 600") ]]'
owner=$(box pgrep -f "^/home/sbx/owner/omakade 600")
mkdir -p "${STATE%/*}"
printf '{"version":2,"phase":"active","owner_pid":%s,"owner_start":-1,"games":[],"streams":[],"window_fullscreen":-1,"window_fullscreen_client":-1}\n' "$owner" >"$STATE"
start_app
press F11
until_true 10 '[[ $(window fullscreen) == 2 ]]'
expect "the library went fullscreen" "$(window fullscreen)" 2
expect "without moving to a Game Mode workspace" "$(window_workspace)" 1
expect "the other session was left alone" "$(session_field owner_pid)" "$owner"
press F11
until_true 5 '[[ $(window fullscreen) == 0 ]]'
expect "F11 returned to the desktop window" "$(window fullscreen)" 0
quit_app
box kill "$owner"
rm -f "$STATE" "$HOME_DIR/owner/omakade"

printf '\n%d passed, %d failed\n' "$passed" "$failed"
((failed == 0))
