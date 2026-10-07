import QtQuick
import QtQuick.Effects
import "../components"

PageRhythm {
  id: root
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var game: d.game || {}
  readonly property bool hasGame: !!d.game
  readonly property bool steam: game.kind === "steam"
  readonly property bool emulator: game.kind === "emulator"
  readonly property var ach: game.achievements || null
  property var rows: [[resume], [extra, notes, pause], [achievementsCard], [quit]]
  hero: banner
  // Hero art keeps its own shape; without it the banner is a compact cover header.
  heroMinimum: banner.wide ? Math.round(width / 2.5) : g.s(150)
  heroMaximum: heroMinimum

  function duration(m) {
    if (m === undefined) return ""
    var h = Math.floor(m / 60), r = m % 60
    return h > 0 ? (r > 0 ? h + " h " + r + " min" : h + " h") : r + " min"
  }

  Item {
    id: banner
    visible: root.hasGame
    width: parent.width; height: root.heroMinimum + root.heroExtra
    readonly property string art: root.game.banner || ""
    // Steam's header capsule has the title painted in: never crop it, use the cover header.
    readonly property bool wide: art !== "" && root.game.bannerKind !== "header" && heroArt.status !== Image.Error
    readonly property string wash: art || root.game.cover || (root.d.capture || {}).lastShot || ""
    readonly property string playtime: [root.game.sessionMinutes !== undefined ? root.duration(root.game.sessionMinutes) + " this session" : "",
      root.game.totalMinutes !== undefined ? root.duration(root.game.totalMinutes) + " played" : ""].filter(function(x) { return x !== "" }).join("  ·  ")

    // The wash: the art decoded tiny and blurred, so any shape fills the banner.
    Image { id: washImage; anchors.fill: parent; source: banner.wide ? "" : banner.wash; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 24; visible: false }
    Rectangle { id: bannerMask; anchors.fill: parent; radius: root.g.radius; visible: false; layer.enabled: true }
    Rectangle { anchors.fill: parent; radius: root.g.radius; color: root.g.well; visible: !banner.wide }
    MultiEffect {
      anchors.fill: parent; source: washImage
      visible: !banner.wide && washImage.status === Image.Ready
      autoPaddingEnabled: false
      blurEnabled: true; blur: 1; blurMax: 64; brightness: -0.28; saturation: 0.05
      maskEnabled: true; maskSource: bannerMask; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0
    }
    Picture {
      id: heroArt
      g: root.g
      anchors.fill: parent
      radius: root.g.radius
      visible: banner.wide
      source: banner.art !== "" && root.game.bannerKind !== "header" ? banner.art : ""
    }
    Rectangle {
      anchors.fill: parent
      visible: banner.wide
      radius: root.g.radius
      gradient: Gradient {
        GradientStop { position: 0.35; color: "transparent" }
        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, Math.max(0.72, root.g.bannerScrim)) }
      }
    }
    Rectangle { anchors.fill: parent; radius: root.g.radius; color: "transparent"; border.width: 1; border.color: root.g.line }

    // Hero: the game's logo over its art, like the library's details page.
    Column {
      visible: banner.wide
      anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
      anchors.margins: root.g.s(16)
      spacing: root.g.s(8)
      Image {
        id: heroLogo
        readonly property bool shown: !!root.game.logo && status !== Image.Error
        visible: shown
        width: Math.min(parent.width * 0.62, root.g.s(300)); height: visible ? Math.min(root.g.s(76), banner.height * 0.42) : 0
        source: root.game.logo || ""
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft; verticalAlignment: Image.AlignBottom
        asynchronous: true
        sourceSize.height: root.g.s(76) * 2
        layer.enabled: shown
        layer.effect: MultiEffect { shadowEnabled: true; shadowBlur: 0.7; shadowOpacity: 0.7; shadowVerticalOffset: root.g.s(2) }
      }
      Label { g: root.g; role: "display"; color: root.g.bannerInk; font.pixelSize: root.g.f(28); text: root.game.title || ""; width: parent.width; elide: Text.ElideRight; visible: !heroLogo.shown }
      Row {
        spacing: root.g.s(8)
        Glyph { g: root.g; name: root.steam ? root.g.icon.steam : root.g.icon.game; size: root.g.f(13); color: root.g.bannerInk; anchors.verticalCenter: parent.verticalCenter }
        Label { g: root.g; role: "small"; color: root.g.bannerInk; text: [root.game.source || "", banner.playtime].filter(function(x) { return x !== "" }).join("  ·  "); anchors.verticalCenter: parent.verticalCenter }
      }
    }

    // Cover header: the boxart beside the title when there is no hero art.
    Picture {
      id: coverArt
      g: root.g
      visible: !banner.wide && !!root.game.cover
      x: root.g.s(14); anchors.verticalCenter: parent.verticalCenter
      height: parent.height - root.g.s(28); width: Math.round(height * 2 / 3)
      source: visible ? root.game.cover : ""
    }
    Column {
      visible: !banner.wide
      anchors.left: coverArt.visible ? coverArt.right : parent.left
      anchors.leftMargin: root.g.s(16)
      anchors.right: parent.right; anchors.rightMargin: root.g.s(16)
      anchors.verticalCenter: parent.verticalCenter
      spacing: root.g.s(6)
      Row {
        spacing: root.g.s(6)
        Glyph { g: root.g; name: root.steam ? root.g.icon.steam : root.g.icon.game; size: root.g.f(13); color: root.g.bannerInk; anchors.verticalCenter: parent.verticalCenter }
        Label { g: root.g; role: "caps"; color: root.g.bannerInk; text: root.game.source || ""; anchors.verticalCenter: parent.verticalCenter }
      }
      Label { g: root.g; role: "display"; color: root.g.bannerInk; font.pixelSize: root.g.f(28); text: root.game.title || ""; width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
      Label { g: root.g; role: "small"; color: root.g.bannerInk; text: banner.playtime.replace("  ·  ", "\n"); width: parent.width; visible: text !== "" }
    }
  }

  Action {
    id: resume
    g: root.g; width: parent.width
    variant: "primary"; icon: root.g.icon.play
    title: root.hasGame ? "Resume" : "Continue playing"
    detail: root.game.paused !== undefined ? (root.game.paused ? "Paused while the guide is open" : "Back to " + (root.game.title || "the game")) : root.game.pauseWhileOpen ? "Paused while the guide is open" : "Back to " + (root.game.title || "the game")
    hintFamily: root.family; hintButton: "b"
    onTriggered: root.act("resume", null)
  }

  Row {
    visible: root.hasGame
    width: parent.width; spacing: root.g.s(10)
    Action {
      id: extra
      visible: !root.emulator || !!root.game.canBackup
      g: root.g; width: (parent.width - parent.spacing * ((notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))) / (1 + (notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))
      variant: "tile"; height: root.g.s(96)
      icon: root.emulator ? root.g.icon.save : root.steam ? root.g.icon.steam : root.g.icon.desktop
      title: root.emulator ? "Saves" : root.steam ? "Steam" : "Desktop"
      detail: root.emulator ? "Back up" : root.steam ? "Overlay" : "Return"
      onTriggered: root.act(root.emulator ? "backup" : root.steam ? "steam-overlay" : "desktop", null)
    }
    Action {
      id: notes
      visible: root.game.notes !== undefined || root.game.note !== undefined
      g: root.g; width: (parent.width - parent.spacing * ((notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))) / (1 + (notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))
      variant: "tile"; height: root.g.s(96); icon: root.g.icon.notes
      title: "Notes"; detail: root.game.note !== undefined ? (root.game.note ? "Saved" : "Add a note") : (root.game.notes ? root.game.notes.length : root.game.note ? 1 : 0) + ((root.game.notes ? root.game.notes.length : root.game.note ? 1 : 0) === 1 ? " note" : " notes")
      onTriggered: root.act("notes", null)
    }
    Action {
      id: pause
      g: root.g; width: (parent.width - parent.spacing * ((notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))) / (1 + (notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))
      variant: "tile"; height: root.g.s(96); icon: root.g.icon.pausedGame
      selected: !!root.game.pauseWhileOpen
      title: "Pause"; detail: root.game.pauseWhileOpen ? "On" : "Off"
      onTriggered: root.act("pause-while-open", !root.game.pauseWhileOpen)
    }
  }

  Rectangle {
    id: achievementsCard
    visible: !!root.ach
    property bool cursor: false
    signal triggered()
    function activate() { root.act("achievements", null) }
    function step(d) { return false }
    readonly property var recent: root.ach && root.ach.items ? root.ach.items.filter(function(a) { return a.unlocked }).slice(0, 3) : []
    width: parent.width; height: achievements.height + root.g.s(28)
    radius: root.g.radius; color: root.g.well
    border.width: 1; border.color: root.g.line
    Column {
      id: achievements
      x: root.g.s(14); y: root.g.s(14); width: parent.width - root.g.s(28)
      spacing: root.g.s(12)
      Item {
        width: parent.width; height: Math.max(achTitle.height, root.g.s(22))
        Glyph { id: achIcon; g: root.g; name: root.g.icon.trophy; size: root.g.f(18); color: root.g.accent; anchors.verticalCenter: parent.verticalCenter }
        Label { id: achTitle; g: root.g; role: "body"; font.bold: true; text: "Achievements"; anchors.left: achIcon.right; anchors.leftMargin: root.g.s(10); anchors.verticalCenter: parent.verticalCenter }
        Row {
          anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
          spacing: root.g.s(8)
          Label { g: root.g; role: "small"; text: root.ach ? root.ach.unlocked + " of " + root.ach.total : ""; anchors.verticalCenter: parent.verticalCenter }
          Glyph { g: root.g; name: root.g.icon.chevron; size: root.g.f(16); color: root.g.dim; anchors.verticalCenter: parent.verticalCenter; visible: !!(root.ach && root.ach.items && root.ach.items.length) }
        }
      }
      Rectangle {
        width: parent.width; height: Math.max(3, root.g.s(4)); radius: height / 2; color: root.g.track
        Rectangle { height: parent.height; radius: parent.radius; color: root.g.accent; width: Math.max(height, parent.width * (root.ach && root.ach.total ? root.ach.unlocked / root.ach.total : 0)) }
      }
      Repeater {
        model: achievementsCard.recent
        delegate: Row {
          required property var modelData
          width: achievements.width; spacing: root.g.s(10)
          Picture { g: root.g; width: root.g.s(34); height: width; radius: root.g.s(6); source: modelData.icon || ""; visible: !!modelData.icon }
          Rectangle { width: root.g.s(34); height: width; radius: root.g.s(6); color: root.g.track; visible: !modelData.icon
            Glyph { g: root.g; anchors.centerIn: parent; name: root.g.icon.trophy; size: root.g.f(15); color: root.g.accent } }
          Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - root.g.s(44)
            Label { g: root.g; role: "body"; width: parent.width; text: modelData.title || ""; elide: Text.ElideRight }
            Label { g: root.g; role: "caption"; width: parent.width; text: modelData.when || ""; elide: Text.ElideRight; visible: text !== "" }
          }
        }
      }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: achievementsCard.activate() }
  }

  bottomBlock: Action {
    id: quit
    visible: root.hasGame || !!(root.d.window || {}).address
    g: root.g; width: parent.width; height: root.g.s(54)
    icon: root.g.icon.power; danger: true
    title: root.game.forceReady ? "Force quit game" : "Quit game"; trailing: "Asks first"
    onTriggered: root.act("quit", null)
  }
}
