import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// What the panel shows, top to bottom: the status line, the game, Resume,
// Desktop and the RetroArch menu where they apply, the capture tiles, sound,
// performance, then Quit under a rule and what the buttons do. Sections that
// do not apply are left out. The quit question takes Quit's place. `g` is the
// guide: its state, colours, sizes and actions. The cursor moves as
// GuideFocus.js says.
Column {
  id: card

  property var g
  // The card's padding, for rules that run out to its edges.
  property int bleed: 0
  readonly property int readoutCount: g.readouts.length
  readonly property bool titleTruncated: hero.truncated
  readonly property int gap: g.sized(Style.space(12))

  spacing: g.sized(Style.space(20))

  StatusRow {
    g: card.g
    width: parent.width
  }

  GameHero {
    id: hero
    g: card.g
    width: parent.width
  }

  ActionButton {
    readonly property var spec: card.g.rowSpec("resume")
    visible: !!card.g.game
    g: card.g
    key: "resume"
    width: parent.width
    height: card.g.sized(Style.space(60))
    icon: spec.icon
    iconSize: Math.round(card.g.sized(Style.font.iconLarge) * 1.15)
    label: spec.label
    labelSize: card.g.sized(Style.font.heading)
    strong: true
    hint: card.g.buttons[1]
  }

  // Desktop (Game Mode) and the RetroArch menu: buttons like the tiles, one
  // line high.
  Row {
    id: extras
    readonly property var keys: [].concat(card.g.showDesktop ? ["desktop"] : [], card.g.showRetroarch ? ["retroarch"] : [])
    visible: extras.keys.length > 0
    width: parent.width
    spacing: card.gap
    Repeater {
      model: extras.keys
      ActionButton {
        required property string modelData
        readonly property var spec: card.g.rowSpec(modelData)
        g: card.g
        key: modelData
        width: Math.floor((extras.width - extras.spacing * (extras.keys.length - 1)) / extras.keys.length)
        height: card.g.sized(Style.space(48))
        icon: spec.icon
        label: spec.label
      }
    }
  }

  // Capture. Save N s joins as a third tile while a replay buffer runs.
  Row {
    id: tiles
    readonly property var keys: card.g.grid.filter(function(row) { return row.indexOf("screenshot") >= 0 })[0] || []
    width: parent.width
    spacing: card.gap
    Repeater {
      model: tiles.keys
      Tile {
        required property string modelData
        readonly property var spec: card.g.rowSpec(modelData)
        g: card.g
        key: modelData
        urgent: !!spec.urgent
        tone: spec.urgent ? "tint" : "plain"
        width: Math.floor((tiles.width - tiles.spacing * (tiles.keys.length - 1)) / tiles.keys.length)
        height: card.g.sized(Style.space(90))
        icon: spec.icon
        label: spec.label
        hint: modelData === "screenshot" ? card.g.buttons[2] : ""
      }
    }
  }

  Column {
    width: parent.width
    visible: card.g.volumeAvailable || card.g.outputs.length > 1
    spacing: card.g.sized(Style.space(10))
    SectionHead {
      g: card.g
      text: "SOUND"
      value: !card.g.volumeAvailable ? "" : card.g.muted ? "Muted" : Math.round(card.g.volume * 100) + "%"
    }
    ControlRow {
      readonly property var spec: card.g.rowSpec("volume")
      visible: card.g.volumeAvailable
      g: card.g
      key: "volume"
      width: parent.width
      kind: "slider"
      icon: spec.icon
      amount: card.g.volume
      dim: card.g.muted
    }
    // The output the game plays through; A or left and right pick the next.
    ControlRow {
      readonly property var spec: card.g.rowSpec("output")
      visible: card.g.outputs.length > 1
      g: card.g
      key: "output"
      width: parent.width
      kind: "text"
      icon: spec.icon
      iconSize: card.g.sized(Style.font.icon)
      label: spec.label
      value: spec.value
    }
  }

  Column {
    width: parent.width
    visible: card.readoutCount > 0
    spacing: card.g.sized(Style.space(10))
    SectionHead {
      g: card.g
      text: "PERFORMANCE"
    }
    // One box per reading, in one row where their figures fit, else two.
    Grid {
      id: readings
      readonly property int widest: {
        var w = 0
        for (var i = 0; i < boxes.count; i++) if (boxes.itemAt(i)) w = Math.max(w, boxes.itemAt(i).naturalWidth)
        return w
      }
      readonly property int fitted: Math.floor((width - spacing * (card.readoutCount - 1)) / Math.max(1, card.readoutCount))
      width: parent.width
      spacing: card.gap
      columns: card.readoutCount > 2 && readings.widest > readings.fitted ? 2 : Math.max(1, card.readoutCount)
      Repeater {
        id: boxes
        model: card.g.readouts
        Readout {
          required property var modelData
          g: card.g
          width: Math.floor((readings.width - readings.spacing * (readings.columns - 1)) / readings.columns)
          label: modelData.label
          value: modelData.value
          unit: modelData.unit
        }
      }
    }
  }

  Sep {
    g: card.g
    bleed: card.bleed
    visible: !!card.g.game
  }

  ActionButton {
    readonly property var spec: card.g.rowSpec("quit")
    visible: !!card.g.game && card.g.view !== "confirm"
    g: card.g
    key: "quit"
    urgent: true
    tone: "outline"
    width: parent.width
    height: card.g.sized(Style.space(50))
    icon: spec.icon
    label: spec.label
  }

  ConfirmBlock {
    visible: !!card.g.game && card.g.view === "confirm"
    g: card.g
    width: parent.width
  }

  HintLine {
    g: card.g
    anchors.horizontalCenter: parent.horizontalCenter
  }
}
