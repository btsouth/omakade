import QtQuick
import "../components"

Column {
  id: root
  required property var g
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var game: d.game || {}
  readonly property bool steam: game.kind === "steam"
  readonly property bool emulator: game.kind === "emulator"
  readonly property var ach: game.achievements || null
  readonly property var session: ((d.capture || {}).recent || []).filter(function(c) { return c.session }).slice(0, 3)
  property var rows: [[resume], [extra, notes], [shot0, shot1, shot2], [pause], [quit]]

  spacing: g.s(20)

  function duration(m) {
    if (m === undefined) return ""
    var h = Math.floor(m / 60), r = m % 60
    return h > 0 ? (r > 0 ? h + " h " + r + " min" : h + " h") : r + " min"
  }

  Row {
    width: parent.width
    spacing: root.g.s(18)
    Picture {
      id: cover
      g: root.g
      source: root.game.cover || ""
      width: root.g.s(138); height: Math.round(width * 4 / 3)
      radius: root.g.radius
    }
    Column {
      width: parent.width - cover.width - parent.spacing
      anchors.bottom: cover.bottom
      spacing: root.g.s(4)
      Row {
        spacing: root.g.s(6)
        Glyph { g: root.g; name: root.steam ? root.g.icon.steam : root.g.icon.game; size: root.g.f(14); color: root.g.dim; anchors.verticalCenter: parent.verticalCenter }
        Label { g: root.g; role: "caps"; text: root.game.source || ""; anchors.verticalCenter: parent.verticalCenter }
      }
      Label {
        g: root.g; role: "display"; text: root.game.title || ""
        width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2
        lineHeight: 0.95
      }
      Item { width: 1; height: root.g.s(4) }
      Label { g: root.g; role: "body"; text: root.duration(root.game.sessionMinutes) + " this session"; width: parent.width }
      Label { g: root.g; role: "small"; text: root.duration(root.game.totalMinutes) + " played in total"; width: parent.width }
    }
  }

  Column {
    visible: !!root.ach
    width: parent.width
    spacing: root.g.s(8)
    Meter {
      width: parent.width
      g: root.g
      label: "Achievements"
      value: root.ach ? root.ach.unlocked + " / " + root.ach.total : ""
      progress: root.ach ? root.ach.unlocked / root.ach.total : 0
    }
    Row {
      spacing: root.g.s(6)
      visible: !!(root.ach && root.ach.latest)
      Glyph { g: root.g; name: root.g.icon.trophy; size: root.g.f(14); color: root.g.accent; anchors.verticalCenter: parent.verticalCenter }
      Label { g: root.g; role: "small"; color: root.g.foreground; text: root.ach && root.ach.latest ? root.ach.latest.name : ""; anchors.verticalCenter: parent.verticalCenter }
      Label { g: root.g; role: "small"; text: root.ach && root.ach.latest ? "· " + root.ach.latest.ago : ""; anchors.verticalCenter: parent.verticalCenter }
    }
    // The locked achievement closest to done, when the source reports progress.
    Item {
      visible: !!(root.ach && root.ach.closest)
      width: parent.width
      height: closest.height + root.g.s(4)
      Meter {
        id: closest
        y: root.g.s(4)
        width: parent.width
        g: root.g
        label: root.ach && root.ach.closest ? "Almost: " + root.ach.closest.name + " · " + root.ach.closest.description : ""
        value: root.ach && root.ach.closest ? root.ach.closest.progressText : ""
        progress: root.ach && root.ach.closest ? root.ach.closest.progress : 0
        fill: root.g.foreground
      }
    }
  }

  Column {
    width: parent.width
    spacing: root.g.s(10)

    Action {
      id: resume
      g: root.g; width: parent.width
      variant: "primary"; icon: root.g.icon.play
      title: "Resume"
      detail: root.game.paused ? "Paused while the guide is open" : "Back to " + (root.game.title || "the game")
      hintFamily: root.family; hintButton: "b"
      onTriggered: root.act("resume", null)
    }

    Row {
      width: parent.width
      spacing: root.g.s(10)
      Action {
        id: extra
        g: root.g; width: (parent.width - parent.spacing) / 2
        variant: "tile"
        icon: root.emulator ? root.g.icon.save : root.steam ? root.g.icon.steam : root.g.icon.desktop
        title: root.emulator ? "Back up saves" : root.steam ? "Steam overlay" : "Return to desktop"
        detail: root.emulator ? (root.game.lastBackup || "Never backed up") : root.steam ? "Friends, chat, guides" : "The game keeps running"
        onTriggered: root.act(root.emulator ? "backup" : root.steam ? "steam-overlay" : "desktop", null)
      }
      Action {
        id: notes
        g: root.g; width: (parent.width - parent.spacing) / 2
        variant: "tile"; icon: root.g.icon.notes
        title: "Notes"
        detail: root.game.note ? root.game.note : "Nothing yet"
        onTriggered: root.act("notes", null)
      }
    }
  }

  // What you captured this session, one press from the Capture page's tools.
  Column {
    visible: root.session.length > 0
    width: parent.width
    spacing: root.g.s(8)
    Row {
      width: parent.width
      Label { g: root.g; role: "caps"; text: "This session"; width: parent.width / 2 }
      Label { g: root.g; role: "small"; text: root.session.length + (root.session.length === 1 ? " capture" : " captures"); width: parent.width / 2; horizontalAlignment: Text.AlignRight }
    }
    Row {
      width: parent.width
      spacing: root.g.s(10)
      Repeater {
        id: sessionShots
        model: root.session
        delegate: Focusable {
          required property var modelData
          property real radius: root.g.innerRadius
          width: (root.width - root.g.s(20)) / 3
          height: Math.round(width * 9 / 16)
          onTriggered: root.act("open-capture", modelData)
          Picture { anchors.fill: parent; g: root.g; source: parent.modelData.thumb }
          Rectangle {
            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: root.g.s(6)
            width: kind.implicitWidth + root.g.s(10); height: kind.implicitHeight + root.g.s(4)
            radius: root.g.innerRadius
            color: root.g.background
            Label { id: kind; g: root.g; role: "caption"; color: root.g.foreground; anchors.centerIn: parent; text: parent.parent.modelData.kind === "Clip" ? parent.parent.modelData.length : parent.parent.modelData.age }
          }
        }
      }
    }
  }
  property Item shot0: sessionShots.count > 0 ? sessionShots.itemAt(0) : null
  property Item shot1: sessionShots.count > 1 ? sessionShots.itemAt(1) : null
  property Item shot2: sessionShots.count > 2 ? sessionShots.itemAt(2) : null

  Column {
    width: parent.width
    spacing: root.g.s(2)
    ToggleRow {
      id: pause
      g: root.g; width: parent.width
      icon: root.g.icon.pausedGame
      title: "Pause while the guide is open"
      detail: root.game.online ? "Off for online games" : "Freezes the game, resumes with it"
      checked: !!root.game.pauseWhileOpen
      onToggled: function(v) { root.act("pause-while-open", v) }
    }
    Action {
      id: quit
      g: root.g; width: parent.width
      icon: root.g.icon.power; danger: true
      title: "Quit game"
      detail: "Asks first"
      onTriggered: root.act("quit", null)
    }
  }
}
