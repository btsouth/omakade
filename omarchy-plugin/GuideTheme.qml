import QtQuick
import Quickshell.Io
import qs.Commons
import "Contrast.js" as Contrast

// Every colour, size and font the guide draws with. Colours come from the active
// Omarchy theme: the shell's Color roles plus the theme's colors.toml for the
// background ladder and the four colours used for controller face buttons.
// Sizes are the shell's own tokens times one guide scale, so a theme that makes
// the shell roomier makes the guide roomier too.
QtObject {
  id: root

  // 1.25 reads comfortably at a desk; Couch size is for a TV across the room.
  property real scale: 1.25

  function s(px) { return Math.max(1, Math.round(Style.spaceReal(px) * scale)) }
  function f(token) { return Math.max(1, Math.round(token * scale)) }

  readonly property string font: Style.font.family
  readonly property int radius: Style.cornerRadius
  readonly property int innerRadius: Math.max(0, Style.cornerRadius - s(4))

  property var palette: ({})

  readonly property color foreground: Color.foreground
  readonly property color background: Color.background
  readonly property color accent: Color.accent
  readonly property color urgent: Color.urgent
  readonly property color dim: Qt.darker(Color.foreground, 1.4)
  readonly property color base: pick("darker_background", Qt.darker(Color.background, 1.3))
  readonly property bool light: String(palette.mode || "") === "light"
    || Contrast.luminance(Contrast.hex(Color.background)) > 0.4

  readonly property color green: pick("green", Color.accent)
  readonly property color red: pick("red", Color.urgent)
  readonly property color blue: pick("blue", Color.accent)
  readonly property color yellow: pick("yellow", Color.accent)

  readonly property var contrastTheme: ({
    foreground: Contrast.hex(foreground), background: Contrast.hex(background),
    accent: Contrast.hex(accent), urgent: Contrast.hex(urgent), dim: Contrast.hex(dim),
    base: Contrast.hex(base)
  })
  readonly property real tint: Contrast.alpha(contrastTheme)

  // Shared state chrome, from the shell's state tokens.
  readonly property color line: Util.alpha(foreground, light ? 0.16 : 0.12)
  readonly property color well: Util.alpha(foreground, light ? 0.06 : 0.05)
  readonly property color track: Util.alpha(foreground, 0.14)

  function pick(key, fallback) {
    var v = palette[key]
    return (typeof v === "string" && /^#[0-9a-fA-F]{6}$/.test(v)) ? v : fallback
  }

  function parse(text) {
    var out = {}
    String(text || "").split("\n").forEach(function(line) {
      var m = line.match(/^\s*([A-Za-z0-9_]+)\s*=\s*"([^"]*)"/)
      if (m) out[m[1]] = m[2]
    })
    palette = out
  }

  property FileView colorsFile: FileView {
    path: Color.currentThemePath + "/colors.toml"
    watchChanges: true
    onFileChanged: reload()
    onLoaded: { root.retries = 0; root.parse(text()) }
    // A theme switch replaces the whole theme folder, so a read can land in the gap.
    onLoadFailed: if (root.retries < 8) { root.retries++; root.retryTimer.restart() }
  }
  property int retries: 0
  property Timer retryTimer: Timer { interval: 250; onTriggered: root.colorsFile.reload() }

  // Re-read when the shell applies new colours as well.
  property Connections colorWatch: Connections {
    target: Color
    function onBackgroundChanged() { root.colorsFile.reload() }
  }

  // Nerd Font glyphs (Material Design set), shared by every page.
  readonly property QtObject icon: QtObject {
    readonly property string game: "\u{f0eb5}"
    readonly property string capture: "\u{f0100}"
    readonly property string performance: "\u{f04c5}"
    readonly property string audio: "\u{f057e}"
    readonly property string controllers: "\u{f05ba}"
    readonly property string system: "\u{f0493}"
    readonly property string play: "\u{f040a}"
    readonly property string pause: "\u{f03e4}"
    readonly property string next: "\u{f04ad}"
    readonly property string previous: "\u{f04ae}"
    readonly property string replay: "\u{f02da}"
    readonly property string record: "\u{f044b}"
    readonly property string steam: "\u{f04d3}"
    readonly property string notes: "\u{f082e}"
    readonly property string power: "\u{f0425}"
    readonly property string sleep: "\u{f0904}"
    readonly property string trophy: "\u{f0538}"
    readonly property string save: "\u{f0193}"
    readonly property string desktop: "\u{f0379}"
    readonly property string library: "\u{f0570}"
    readonly property string wifi: "\u{f05a9}"
    readonly property string bluetooth: "\u{f00af}"
    readonly property string dnd: "\u{f009b}"
    readonly property string bell: "\u{f009a}"
    readonly property string battery: "\u{f0081}"
    readonly property string charging: "\u{f0084}"
    readonly property string mic: "\u{f036c}"
    readonly property string micOff: "\u{f036d}"
    readonly property string speaker: "\u{f04c3}"
    readonly property string tv: "\u{f0502}"
    readonly property string brightness: "\u{f00df}"
    readonly property string folder: "\u{f024f}"
    readonly property string chevron: "\u{f0142}"
    readonly property string check: "\u{f012c}"
    readonly property string playstation: "\u{f0414}"
    readonly property string nintendo: "\u{f07e1}"
    readonly property string gauge: "\u{f029a}"
    readonly property string pausedGame: "\u{f03e5}"
    readonly property string video: "\u{f0567}"
    readonly property string timer: "\u{f051b}"
  }
}
