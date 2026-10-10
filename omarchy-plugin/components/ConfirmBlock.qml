import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The quit question, in Quit's place: the question, what it costs, then the
// safe choice and the destructive one side by side. "Keep playing" has the
// cursor first, so one stray A cannot quit.
BorderSurface {
  id: block
  property var g
  readonly property int pad: g.sized(Style.space(18))
  readonly property string title: g.game ? g.game.title : "the game"

  radius: g.sized(Style.cornerRadius)
  color: Util.alpha(g.urgentInk, 0.06)
  borderSpec: Border.flat(Util.alpha(g.urgentInk, 0.4), Math.max(1, g.sized(1)))
  implicitHeight: column.implicitHeight + block.pad * 2

  Column {
    id: column
    x: block.pad
    y: block.pad
    width: block.width - block.pad * 2
    spacing: block.g.sized(Style.space(10))

    Text {
      width: parent.width
      textFormat: Text.PlainText
      text: block.g.forceReady ? block.title + " is not closing" : "Quit " + block.title + "?"
      color: block.g.text
      font.family: block.g.fontFamily
      font.pixelSize: block.g.sized(Style.font.heading)
      font.weight: Font.DemiBold
      wrapMode: Text.Wrap
    }

    Text {
      width: parent.width
      textFormat: Text.PlainText
      text: block.g.forceReady ? "Force quit ends it now. Anything you haven't saved is lost."
        : "Anything you haven't saved in the game is lost."
      color: block.g.dim
      font.family: block.g.fontFamily
      font.pixelSize: block.g.sized(Style.font.body)
      wrapMode: Text.Wrap
    }

    Item { width: 1; height: block.g.sized(Style.space(3)) }

    Row {
      id: pair
      width: parent.width
      spacing: block.g.sized(Style.space(10))
      readonly property int cell: Math.floor((width - spacing) / 2)

      ActionButton {
        g: block.g
        width: pair.cell
        height: block.g.sized(Style.space(48))
        name: "keep"
        current: block.g.confirmChoice === 0
        onHovered: (source, mouse) => block.g.hoverConfirm(0, source, mouse)
        centered: true
        label: "Keep playing"
        onActivated: block.g.cancelQuit()
      }
      ActionButton {
        g: block.g
        width: pair.cell
        height: block.g.sized(Style.space(48))
        name: "confirm-quit"
        current: block.g.confirmChoice === 1
        onHovered: (source, mouse) => block.g.hoverConfirm(1, source, mouse)
        urgent: true
        tone: "outline"
        centered: true
        icon: block.g.icons.quit
        label: block.g.forceReady ? "Force quit" : "Quit"
        onActivated: block.g.quitGame()
      }
    }
  }
}
