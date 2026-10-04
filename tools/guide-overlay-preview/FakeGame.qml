import QtQuick
import QtQuick.Window

// A fullscreen "game" for rendering the guide over: one still frame.
Window {
  visible: true
  visibility: Window.FullScreen
  title: "Fake game"
  color: "black"
  Image {
    anchors.fill: parent
    source: Qt.resolvedUrl(Qt.application.arguments[Qt.application.arguments.length - 1].indexOf(".jpg") > 0
      ? "file://" + Qt.application.arguments[Qt.application.arguments.length - 1] : "game.jpg")
    fillMode: Image.PreserveAspectCrop
  }
}
