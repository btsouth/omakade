#!/usr/bin/env bash
# Render the omakade.guide shell plugin inside an omabox box over a still "game".
#   omabox up --plugin "$PWD/omarchy-plugin"
#   tools/guide-overlay-preview/render-plugin.sh OUT_DIR [THEME...]
# For each theme (or the current one): one full-screen shot per tab.
set -euo pipefail
out=${1:?output directory}; shift
here=$(cd "$(dirname "$0")" && pwd)
fixture=${FIXTURE:-$here/fixtures/lantern-road.json}
pad=${PAD:-xbox}
scale=${SCALE:-1.25}
tabs=${TABS:-"game capture performance audio controllers system"}
mkdir -p "$out"

if ! omabox windows 2>/dev/null | grep -q 'Fake game'; then
  omabox run -d --wait -- /usr/lib/qt6/bin/qml "$here/FakeGame.qml" >/dev/null
fi
# A game covers the bar: make the still frame truly fullscreen.
if ! omabox hyprctl -j activewindow | grep -q '"fullscreen": 2'; then
  omabox hyprctl dispatch focuswindow 'title:Fake game' >/dev/null
  omabox hyprctl dispatch fullscreen 0 >/dev/null
  omabox wait still >/dev/null || true
fi

shoot() {
  local name=$1
  omabox run -- omarchy-shell shell hide omakade.guide >/dev/null || true
  omabox wait still >/dev/null || true
  for tab in $tabs; do
    omabox run -- omarchy-shell shell summon omakade.guide \
      "{\"fixture\":\"$fixture\",\"pad\":\"$pad\",\"scale\":$scale,\"tab\":\"$tab\",\"audit\":\"$name\"}" >/dev/null
    omabox wait cmd -- bash -c 'test "$(omarchy-shell omakade.guide ready)" = ready' >/dev/null
    omabox wait still >/dev/null || true
    omabox shot -o "$out/$name-$tab.png" >/dev/null
  done
}

if [ $# -eq 0 ]; then shoot current; else
  for theme in "$@"; do
    omabox run -- omarchy-theme-set "$theme" >/dev/null
    omabox wait still >/dev/null || true
    shoot "$theme"
  done
fi
