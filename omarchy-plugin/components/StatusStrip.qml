import QtQuick
import qs.Commons

// Always at the top: the time, how long this session has run, and the state of
// everything that can change under a game (capture, network, alerts, power).
Item {
  id: root
  required property var g
  property var status: ({})
  property date now: new Date()

  implicitHeight: g.s(60)

  readonly property string clock: status.clock || Qt.formatTime(now, "HH:mm")
  readonly property string date: status.date || Qt.formatDate(now, "ddd d MMM")

  Timer { interval: 1000; running: !root.status.clock; repeat: true; onTriggered: root.now = new Date() }

  Column {
    anchors.left: parent.left
    anchors.verticalCenter: parent.verticalCenter
    spacing: 0
    Label { g: root.g; role: "display"; text: root.clock }
    Label { g: root.g; role: "small"; text: root.date }
  }

  Row {
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    spacing: root.g.s(12)

    Rectangle {
      visible: !!(root.status.recording || (root.status.replay && root.status.replay.on))
      width: captureStatus.width + root.g.s(14); height: root.g.s(24)
      radius: root.g.innerRadius
      color: root.g.well; border.width: 1; border.color: root.g.line
      anchors.verticalCenter: parent.verticalCenter
      Row {
        id: captureStatus
        anchors.centerIn: parent; spacing: root.g.s(6)
        Rectangle { width: root.g.s(6); height: width; radius: width / 2; color: root.g.urgent; anchors.verticalCenter: parent.verticalCenter }
        Label { g: root.g; role: "caps"; color: root.g.foreground; text: root.status.recording ? "REC" + (root.status.recordingTime ? " " + root.status.recordingTime : "") + (root.status.replay && root.status.replay.on ? " · REPLAY" : "") : "REPLAY"; anchors.verticalCenter: parent.verticalCenter }
      }
    }
    Label {
      visible: root.status.fps !== undefined && root.status.fps !== null
      g: root.g; role: "caps"; color: root.g.foreground
      text: root.status.fps + " FPS"
      anchors.verticalCenter: parent.verticalCenter
    }
    Glyph { visible: !!root.status.dnd; g: root.g; name: root.g.icon.dnd; size: root.g.f(16); anchors.verticalCenter: parent.verticalCenter }
    Glyph { visible: !!root.status.bluetooth; g: root.g; name: root.g.icon.bluetooth; size: root.g.f(16); anchors.verticalCenter: parent.verticalCenter }
    Glyph { visible: !!root.status.wifi; g: root.g; name: root.g.icon.wifi; size: root.g.f(16); anchors.verticalCenter: parent.verticalCenter }
    Row {
      visible: !!root.status.battery
      spacing: root.g.s(4)
      anchors.verticalCenter: parent.verticalCenter
      Glyph {
        g: root.g; size: root.g.f(16)
        name: root.status.battery && root.status.battery.charging ? root.g.icon.charging : root.g.icon.battery
        color: root.status.battery && root.status.battery.percent <= 15 ? root.g.urgent : root.g.foreground
        anchors.verticalCenter: parent.verticalCenter
      }
      Label { g: root.g; role: "small"; color: root.g.foreground; text: root.status.battery ? root.status.battery.percent + "%" : ""; anchors.verticalCenter: parent.verticalCenter }
    }
  }
}
