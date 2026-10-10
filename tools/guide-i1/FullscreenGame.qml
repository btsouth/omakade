import QtQuick
import QtQuick.Window

// A moving fullscreen client: its counter stops only when the native guide pauses it.
Window {
  id: game
  visible: true
  visibility: Window.FullScreen
  title: "Guide I1 fullscreen game"
  property int ticks: 0
  property string lastKey: ""
  Item {
    anchors.fill: parent; focus: true
    Keys.onPressed: function(event) {
      game.lastKey = event.key + ":" + event.modifiers
      console.log("TEST_KEY " + game.lastKey)
      event.accepted = true
    }
  }
  Image {
    anchors.fill: parent
    source: "../guide-overlay-preview/game.jpg"
    fillMode: Image.PreserveAspectCrop
  }
  Timer { interval: 100; running: true; repeat: true; onTriggered: game.ticks++ }
  Text {
    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 32
    text: "Live fullscreen client · frame " + game.ticks + " · key " + game.lastKey
    color: "white"
    font.pixelSize: 26
  }
}
