import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The quit question inside the card, laid out as Omarchy's ConfirmDialog: the
// question, what it costs, then the safe choice and the destructive one side by
// side. "Keep playing" has the cursor first.
Column {
  id: block
  property var g
  spacing: g.sized(Style.space(6))

  Text {
    width: parent.width
    textFormat: Text.PlainText
    text: block.g.forceReady ? (block.g.game ? block.g.game.title : "The game") + " is not closing"
      : "Quit " + (block.g.game ? block.g.game.title : "the game") + "?"
    color: block.g.text
    font.family: block.g.fontFamily
    font.pixelSize: block.g.sized(Style.font.heading)
    font.bold: true
    wrapMode: Text.Wrap
  }

  Text {
    width: parent.width
    textFormat: Text.PlainText
    text: block.g.forceReady ? "Force quit ends it now. Unsaved progress is lost."
      : "Progress since your last save may be lost."
    color: block.g.quiet
    font.family: block.g.fontFamily
    font.pixelSize: block.g.sized(Style.font.body)
    wrapMode: Text.Wrap
  }

  Item { width: 1; height: block.g.sized(Style.space(8)) }

  Grid {
    width: parent.width
    columns: 2
    spacing: block.g.sized(Style.space(8))
    readonly property int cell: Math.floor((width - spacing) / 2)

    ActionButton {
      g: block.g
      width: parent.cell
      height: block.g.sized(Style.space(44))
      name: "keep"
      current: block.g.confirmChoice === 0
      onHovered: (source, mouse) => block.g.hoverConfirm(0, source, mouse)
      icon: block.g.icons.resume
      iconScale: 1.3
      label: "Keep playing"
      onActivated: block.g.cancelQuit()
    }
    ActionButton {
      g: block.g
      width: parent.cell
      height: block.g.sized(Style.space(44))
      name: "confirm-quit"
      current: block.g.confirmChoice === 1
      onHovered: (source, mouse) => block.g.hoverConfirm(1, source, mouse)
      urgent: true
      icon: block.g.icons.quit
      label: block.g.forceReady ? "Force quit" : "Quit game"
      onActivated: block.g.quitGame()
    }
  }
}
