#!/usr/bin/env bash
# Render the omakade.guide card inside an omabox box over a still "game".
#   omabox up --plugin "$PWD/omarchy-plugin"
#   tools/guide-overlay-preview/render-plugin.sh OUT_DIR [THEME...]
# One full-screen shot per theme and fixture, named THEME-FIXTURE.png.
# FIXTURES picks fixtures by name (default: all of them), PAD the button names,
# OMABOX_BOX the box (passed as -b).
set -euo pipefail
out=${1:?output directory}; shift
here=$(cd "$(dirname "$0")" && pwd)
pad=${PAD:-xbox}
fixtures=${FIXTURES:-$(cd "$here/fixtures" && ls *.json | sed 's/\.json$//' | tr '\n' ' ')}
box=()
[ -n "${OMABOX_BOX:-}" ] && box=(-b "$OMABOX_BOX")
mkdir -p "$out"

ob() { omabox "${box[@]}" "$@"; }

if ! ob windows 2>/dev/null | grep -q 'Fake game'; then
  ob run -d --wait -- /usr/lib/qt6/bin/qml "$here/FakeGame.qml" >/dev/null
fi
# A game covers the bar: keep the still frame truly fullscreen (a theme switch
# can drop it), and keep the pointer out of the shots.
fullscreen() {
  if ! ob hyprctl -j activewindow | grep -q '"fullscreen": 2'; then
    ob hyprctl dispatch focuswindow 'title:Fake game' >/dev/null
    ob hyprctl dispatch fullscreen 0 >/dev/null
    ob wait still >/dev/null || true
  fi
}
ob lua 'hl.config({cursor={invisible=true}})' >/dev/null 2>&1 || true
ob pointer -- move 4 4 >/dev/null 2>&1 || true

shoot() {
  local name=$1
  for fixture in $fixtures; do
    ob run -- omarchy-shell shell hide omakade.guide >/dev/null || true
    fullscreen
    ob run -- omarchy-shell shell summon omakade.guide \
      "{\"fixture\":\"$here/fixtures/$fixture.json\",\"pad\":\"$pad\"}" >/dev/null
    ob wait cmd -- bash -c 'test "$(omarchy-shell omakade.guide ready)" = ready' >/dev/null
    ob wait still >/dev/null || true
    ob shot -o "$out/$name-$fixture.png" >/dev/null
  done
  ob run -- omarchy-shell shell hide omakade.guide >/dev/null || true
}

if [ $# -eq 0 ]; then shoot current; else
  for theme in "$@"; do
    ob run -- omarchy-theme-set "$theme" >/dev/null
    ob wait still >/dev/null || true
    shoot "$theme"
  done
fi
