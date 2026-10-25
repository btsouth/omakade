import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// Ask: "What now?" sends a picture of the paused game to the user's default agent. At
// rest and while the agent thinks it is one button; with an answer or a failure, the text
// sits above Ask again and, for a first answer, Look it up, which checks it against a
// guide on the web. `g` is the guide.
Column {
  id: block

  property var g
  readonly property var ask: g.ask || ({})
  readonly property bool showText: !g.asking && (ask.state === "answer" || ask.state === "failed") && !!ask.text
  readonly property var keys: g.grid.filter(function(row) { return row.indexOf("ask") >= 0 })[0] || []

  spacing: g.sized(Style.space(10))

  SectionHead {
    visible: block.showText
    g: block.g
    text: block.ask.state === "failed" ? "ASK" : block.ask.mode === "lookup" ? "FROM A GUIDE" : "WHAT NOW"
    value: [block.ask.agent, block.g.askAge].filter(function(v) { return !!v }).join(" · ")
  }

  Text {
    id: answer
    visible: block.showText
    width: parent.width
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
    text: block.ask.text || ""
    color: block.ask.state === "failed" ? block.g.urgentInk : block.g.text
    font.family: block.g.fontFamily
    font.pixelSize: block.g.sized(Style.font.title)
    lineHeight: 1.15
  }

  Row {
    id: buttons
    width: parent.width
    spacing: block.g.sized(Style.space(12))
    Repeater {
      model: block.keys
      ActionButton {
        required property string modelData
        readonly property var spec: block.g.rowSpec(modelData)
        g: block.g
        key: modelData
        width: Math.floor((buttons.width - buttons.spacing * (block.keys.length - 1)) / block.keys.length)
        height: block.g.sized(Style.space(48))
        icon: spec.icon
        label: spec.label
        value: spec.value || ""
        // X asks from anywhere on the card, except while an answer is coming.
        hint: modelData === "ask" && !block.g.asking ? block.g.buttons[3] : ""
      }
    }
  }
}
