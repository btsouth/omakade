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
  property var rows: [[resume], [extra, notes, pause], [quit]]
  hero: banner
  heroMinimum: g.s(222)
  heroMaximum: g.s(292)

  function duration(m) {
    if (m === undefined) return ""
    var h = Math.floor(m / 60), r = m % 60
    return h > 0 ? (r > 0 ? h + " h " + r + " min" : h + " h") : r + " min"
  }

  Item {
    id: banner
    visible: root.hasGame
    width: parent.width; height: root.heroMinimum + root.heroExtra
    clip: true
    readonly property string art: root.game.banner || (root.d.capture || {}).lastShot || root.game.cover || ""
    // A wide hero is cropped from its centre. A Steam header or a cover has the title
    // painted in, so it only tints the backdrop and the cover card carries the art.
    readonly property bool wide: art !== "" && art !== root.game.cover && root.game.bannerKind !== "header"
    // The logo belongs on hero art; beside a cover it would repeat the title painted there.
    readonly property bool hasLogo: wide && !!root.game.logo && logo.status !== Image.Error

    // The same art, blurred and dimmed, fills the banner whatever its shape.
    Image {
      id: backdrop
      anchors.fill: parent
      source: banner.art
      fillMode: Image.PreserveAspectCrop
      asynchronous: true
      // Decoded tiny so painted-in titles melt into colour instead of ghosting.
      sourceSize.width: 24
      visible: false
    }
    MultiEffect {
      anchors.fill: parent
      source: backdrop
      visible: backdrop.status === Image.Ready
      autoPaddingEnabled: false
      blurEnabled: true; blur: 1; blurMax: 64
      brightness: -0.2; saturation: 0.1
    }
    Image {
      anchors.fill: parent
      visible: banner.wide
      source: banner.wide ? banner.art : ""
      fillMode: Image.PreserveAspectCrop
      asynchronous: true
      sourceSize.width: width * 2
    }
    // Without a wide hero the cover stands beside the title, as on the library shelf.
    Picture {
      id: coverCard
      g: root.g
      z: 1
      visible: !banner.wide && !!root.game.cover
      anchors.right: parent.right; anchors.rightMargin: root.g.s(16)
      anchors.verticalCenter: parent.verticalCenter
      height: parent.height - root.g.s(32); width: Math.round(height * 2 / 3)
      source: visible ? root.game.cover : ""
      layer.enabled: visible
      layer.effect: MultiEffect { shadowEnabled: true; shadowBlur: 0.8; shadowOpacity: 0.6; shadowVerticalOffset: root.g.s(4) }
    }
    Rectangle {
      anchors.fill: parent
      gradient: Gradient {
        GradientStop { position: banner.wide ? 0.55 : 0; color: banner.wide ? "transparent" : Qt.rgba(0, 0, 0, root.g.bannerScrim * 0.5) }
        GradientStop { position: Math.max(0.55, (bannerInfo.y - root.g.s(4)) / banner.height); color: Qt.rgba(0, 0, 0, root.g.bannerScrim) }
        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, root.g.bannerScrim) }
      }
    }
    Column {
      id: bannerInfo
      anchors.left: parent.left; anchors.right: parent.right
      anchors.bottom: parent.bottom; anchors.bottomMargin: root.g.s(14)
      anchors.leftMargin: root.g.s(14)
      anchors.rightMargin: coverCard.visible ? coverCard.width + root.g.s(30) : root.g.s(14)
      spacing: root.g.s(5)
      Row {
        spacing: root.g.s(6)
        Glyph { g: root.g; name: root.steam ? root.g.icon.steam : root.g.icon.game; size: root.g.f(13); color: root.g.bannerInk; anchors.verticalCenter: parent.verticalCenter }
        Label { g: root.g; role: "caps"; color: root.g.bannerInk; text: root.game.source || ""; anchors.verticalCenter: parent.verticalCenter }
      }
      Image {
        id: logo
        visible: banner.hasLogo
        width: parent.width * 0.72; height: visible ? root.g.s(84) : 0
        source: root.game.logo || ""
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft; verticalAlignment: Image.AlignBottom
        asynchronous: true
        sourceSize.height: root.g.s(84) * 2
        layer.enabled: visible
        layer.effect: MultiEffect { shadowEnabled: true; shadowBlur: 0.6; shadowOpacity: 0.55; shadowVerticalOffset: root.g.s(2) }
      }
      Label { g: root.g; role: "display"; color: root.g.bannerInk; font.pixelSize: root.g.f(30); text: root.game.title || ""; width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2; visible: !banner.hasLogo }
      Label { g: root.g; role: "small"; color: root.g.bannerInk; text: [root.game.sessionMinutes !== undefined ? root.duration(root.game.sessionMinutes) + " this session" : "", root.game.totalMinutes !== undefined ? root.duration(root.game.totalMinutes) + " total" : ""].filter(function(x) { return x !== "" }).map(function(x) { return x.replace(/ /g, "\u00a0") }).join(" · "); width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2 }
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
      variant: "tile"; height: root.g.s(116)
      icon: root.emulator ? root.g.icon.save : root.steam ? root.g.icon.steam : root.g.icon.desktop
      title: root.emulator ? "Saves" : root.steam ? "Steam" : "Desktop"
      detail: root.emulator ? "Back up" : root.steam ? "Overlay" : "Return"
      onTriggered: root.act(root.emulator ? "backup" : root.steam ? "steam-overlay" : "desktop", null)
    }
    Action {
      id: notes
      visible: root.game.notes !== undefined || root.game.note !== undefined
      g: root.g; width: (parent.width - parent.spacing * ((notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))) / (1 + (notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))
      variant: "tile"; height: root.g.s(116); icon: root.g.icon.notes
      title: "Notes"; detail: root.game.note !== undefined ? (root.game.note ? "Saved" : "Add a note") : (root.game.notes ? root.game.notes.length : root.game.note ? 1 : 0) + ((root.game.notes ? root.game.notes.length : root.game.note ? 1 : 0) === 1 ? " note" : " notes")
      onTriggered: root.act("notes", null)
    }
    Action {
      id: pause
      g: root.g; width: (parent.width - parent.spacing * ((notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))) / (1 + (notes.visible ? 1 : 0) + (extra.visible ? 1 : 0))
      variant: "tile"; height: root.g.s(116); icon: root.g.icon.pausedGame
      selected: !!root.game.pauseWhileOpen
      title: "Pause"; detail: root.game.pauseWhileOpen ? "On" : "Off"
      onTriggered: root.act("pause-while-open", !root.game.pauseWhileOpen)
    }
  }

  Rectangle {
    visible: !!root.ach
    width: parent.width; height: achievements.height + root.g.s(28)
    radius: root.g.radius; color: root.g.well
    border.width: 1; border.color: root.g.line
    Column {
      id: achievements
      x: root.g.s(14); y: root.g.s(14); width: parent.width - root.g.s(28)
      spacing: root.g.s(12)
      Meter { g: root.g; width: parent.width; label: "Achievements"; value: root.ach ? root.ach.unlocked + " / " + root.ach.total : ""; progress: root.ach && root.ach.total ? root.ach.unlocked / root.ach.total : 0 }
      Row {
        width: parent.width; spacing: root.g.s(8)
        visible: !!(root.ach && root.ach.latest)
        Glyph { g: root.g; name: root.g.icon.trophy; size: root.g.f(18); color: root.g.accent; anchors.verticalCenter: parent.verticalCenter }
        Column {
          width: parent.width - root.g.s(30)
          Label { g: root.g; role: "body"; width: parent.width; text: root.ach && root.ach.latest ? root.ach.latest.name : "" }
          Label { g: root.g; role: "caption"; text: root.ach && root.ach.latest ? "Latest unlock · " + root.ach.latest.ago : "" }
        }
      }
      Meter {
        visible: !!(root.ach && root.ach.closest)
        g: root.g; width: parent.width
        label: root.ach && root.ach.closest ? "Next · " + root.ach.closest.name : ""
        value: root.ach && root.ach.closest ? root.ach.closest.progressText : ""
        progress: root.ach && root.ach.closest ? root.ach.closest.progress : 0
        fill: root.g.foreground
      }
    }
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
