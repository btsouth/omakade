import QtQuick
import qs.Commons
import qs.Commons as Commons

// The top line: on the left PAUSED in the selected colour, or REC and the
// clip's time in the urgent colour while one runs; on the right the pad, with
// its battery when known, and the clock.
Item {
  id: status
  property var g
  readonly property bool recording: !!g.recording
  readonly property bool marked: status.recording || !!(g.game && g.game.paused)
  readonly property color markInk: status.recording ? g.urgentInk : g.accentInk
  width: parent ? parent.width : 0
  implicitHeight: Math.max(chip.height, right.implicitHeight)
  // Width left for the pad's name: the line less the chip, a gap as wide as the right
  // side's spacing, the clock and the pad's glyph.
  readonly property real padRoom: status.width - (chip.visible ? chip.width + right.spacing : 0) -
    right.spacing - clock.implicitWidth - padGlyph.width - pad.spacing

  TextMetrics {
    id: fullPad
    font: padText.font
    text: status.g.padText
  }

  Rectangle {
    id: chip
    visible: status.marked
    anchors.verticalCenter: parent.verticalCenter
    width: mark.implicitWidth + status.g.sized(Style.space(10)) * 2
    height: mark.implicitHeight + status.g.sized(Style.space(5)) * 2
    radius: Math.min(status.g.sized(Style.cornerRadius), status.g.sized(Style.space(6)))
    color: Util.alpha(status.markInk, 0.12)

    Row {
      id: mark
      anchors.centerIn: parent
      spacing: status.g.sized(Style.space(7))
      Rectangle {
        visible: status.recording
        anchors.verticalCenter: parent.verticalCenter
        width: status.g.sized(Style.space(7))
        height: width
        radius: width / 2
        color: status.markInk
      }
      InkGlyph {
        visible: !status.recording
        width: status.g.sized(Style.space(12))
        height: chipText.height
        text: status.g.icons.pause
        color: status.markInk
        size: status.g.sized(Style.font.body)
        fontFamily: status.g.fontFamily
      }
      Text {
        id: chipText
        textFormat: Text.PlainText
        text: status.recording ? "REC " + status.g.recordingTime : "PAUSED"
        color: status.markInk
        font.family: status.g.fontFamily
        font.pixelSize: status.g.sized(Style.font.body)
        font.letterSpacing: Math.max(0.5, status.g.sized(Style.font.body) * 0.06)
      }
    }
  }

  Row {
    id: right
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    spacing: status.g.sized(Style.space(18))

    Row {
      id: pad
      spacing: status.g.sized(Style.space(7))
      anchors.verticalCenter: parent.verticalCenter
      InkGlyph {
        id: padGlyph
        width: status.g.sized(Style.space(18))
        height: padText.height
        text: status.g.family === "keyboard" ? status.g.icons.keyboard : status.g.icons.gamepad
        color: status.g.dim
        size: status.g.sized(Style.font.iconLarge)
        fontFamily: status.g.fontFamily
      }
      Text {
        id: padText
        textFormat: Text.PlainText
        // The brand when it fits beside the chip and the clock, else just "Controller".
        text: fullPad.advanceWidth <= status.padRoom ? status.g.padText : status.g.padShortText
        color: status.g.dim
        font.family: status.g.fontFamily
        font.pixelSize: status.g.sized(Style.font.subtitle)
      }
    }

    Text {
      id: clock
      anchors.verticalCenter: parent.verticalCenter
      textFormat: Text.PlainText
      text: status.g.clock
      color: status.g.text
      font.family: status.g.fontFamily
      font.pixelSize: status.g.sized(Style.font.subtitle)
    }
  }
}
