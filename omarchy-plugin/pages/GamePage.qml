import QtQuick
import "../components"

PageRhythm {
  id: root
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var game: d.game || {}
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
    width: parent.width; height: root.heroMinimum + root.heroExtra
    clip: true
    // Focus the crop on the moon and market, clear of the left-hand windows.
    Image {
      anchors.centerIn: parent
      anchors.horizontalCenterOffset: -parent.width * 0.15
      width: parent.width * 1.4; height: parent.height * 1.4
      source: root.game.banner || (root.d.capture || {}).lastShot || root.game.cover || ""
      fillMode: Image.PreserveAspectCrop
      horizontalAlignment: Image.AlignRight
    }
    Rectangle {
      anchors.fill: parent
      gradient: Gradient {
        GradientStop { position: 0.55; color: "transparent" }
        GradientStop { position: Math.max(0.55, (bannerInfo.y - root.g.s(4)) / banner.height); color: Qt.rgba(0, 0, 0, root.g.bannerScrim) }
        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, root.g.bannerScrim) }
      }
    }
    Column {
      id: bannerInfo
      anchors.left: parent.left; anchors.right: parent.right
      anchors.bottom: parent.bottom; anchors.bottomMargin: root.g.s(14)
      anchors.leftMargin: root.g.s(14); anchors.rightMargin: root.g.s(14)
      spacing: root.g.s(5)
      Row {
        spacing: root.g.s(6)
        Glyph { g: root.g; name: root.steam ? root.g.icon.steam : root.g.icon.game; size: root.g.f(13); color: root.g.bannerInk; anchors.verticalCenter: parent.verticalCenter }
        Label { g: root.g; role: "caps"; color: root.g.bannerInk; text: root.game.source || ""; anchors.verticalCenter: parent.verticalCenter }
      }
      Label { g: root.g; role: "display"; color: root.g.bannerInk; font.pixelSize: root.g.f(30); text: root.game.title || ""; width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2 }
      Label { g: root.g; role: "small"; color: root.g.bannerInk; text: root.duration(root.game.sessionMinutes) + " this session · " + root.duration(root.game.totalMinutes) + " total"; width: parent.width }
    }
  }

  Action {
    id: resume
    g: root.g; width: parent.width
    variant: "primary"; icon: root.g.icon.play
    title: "Resume"
    detail: root.game.pauseWhileOpen ? "Paused while the guide is open" : "Back to " + (root.game.title || "the game")
    hintFamily: root.family; hintButton: "b"
    onTriggered: root.act("resume", null)
  }

  Row {
    width: parent.width; spacing: root.g.s(10)
    Action {
      id: extra
      g: root.g; width: (parent.width - parent.spacing * 2) / 3
      variant: "tile"; height: root.g.s(116)
      icon: root.emulator ? root.g.icon.save : root.steam ? root.g.icon.steam : root.g.icon.desktop
      title: root.emulator ? "Saves" : root.steam ? "Steam" : "Desktop"
      detail: root.emulator ? "Back up" : root.steam ? "Overlay" : "Return"
      onTriggered: root.act(root.emulator ? "backup" : root.steam ? "steam-overlay" : "desktop", null)
    }
    Action {
      id: notes
      g: root.g; width: (parent.width - parent.spacing * 2) / 3
      variant: "tile"; height: root.g.s(116); icon: root.g.icon.notes
      title: "Notes"; detail: (root.game.notes ? root.game.notes.length : root.game.note ? 1 : 0) + ((root.game.notes ? root.game.notes.length : root.game.note ? 1 : 0) === 1 ? " note" : " notes")
      onTriggered: root.act("notes", null)
    }
    Action {
      id: pause
      g: root.g; width: (parent.width - parent.spacing * 2) / 3
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
    g: root.g; width: parent.width; height: root.g.s(54)
    icon: root.g.icon.power; danger: true
    title: "Quit game"; trailing: "Asks first"
    onTriggered: root.act("quit", null)
  }
}
