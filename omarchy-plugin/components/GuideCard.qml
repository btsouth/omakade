import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// What the card shows, in the language of Omarchy's audio, monitor and weather
// panels: a hero with the game, its readings as labelled numbers, a row of
// action tiles, achievements and sound as panel sections, then Resume and Quit
// as a pair of buttons, and what the buttons do. The achievements list takes
// the place of the tiles and sections; the quit question takes the place of
// everything under the readings. `g` is the guide: its state, colours, sizes
// and actions. The cursor moves as GuideFocus.js says.
Column {
  id: card

  property var g
  readonly property alias achievementList: list
  readonly property int readoutCount: g.readouts.length
  readonly property bool titleTruncated: hero.truncated

  GameHero {
    id: hero
    g: card.g
    width: parent.width
  }

  Sep { g: card.g; visible: card.readoutCount > 0 }

  // The readings, one column each, centred.
  Row {
    id: readings
    visible: card.readoutCount > 0
    width: parent.width
    spacing: tiles.spacing
    Repeater {
      model: card.g.readouts
      Readout {
        required property var modelData
        g: card.g
        width: Math.floor((readings.width - readings.spacing * (card.readoutCount - 1)) / Math.max(1, card.readoutCount))
        centered: true
        label: modelData.label
        value: modelData.value
        unit: modelData.unit
      }
    }
  }

  Sep { g: card.g }

  Item {
    id: body
    width: parent.width
    height: card.g.view === "confirm" ? confirm.implicitHeight : main.implicitHeight

    Column {
      id: main
      width: parent.width
      visible: card.g.view === "main"

      // Capture and leaving the game. Save last N s joins as a fifth tile while
      // a replay buffer runs; the tiles narrow, the card keeps its height.
      Row {
        id: tiles
        width: parent.width
        spacing: card.g.sized(Style.space(8))
        Repeater {
          model: card.g.grid.length ? card.g.grid[0] : []
          Tile {
            required property string modelData
            readonly property var spec: card.g.rowSpec(modelData)
            readonly property int count: card.g.grid[0].length
            g: card.g
            key: modelData
            width: Math.floor((tiles.width - tiles.spacing * (count - 1)) / count)
            height: card.g.sized(Style.space(72))
            icon: modelData === "record" && card.g.recording ? card.g.icons.stop : spec.icon
            iconScale: modelData === "record" && !card.g.recording ? 1.1 : 1
            iconColor: spec.iconColor !== undefined ? spec.iconColor : ink
            label: ({screenshot: "Screenshot",
                     record: card.g.recording ? "Stop " + card.g.recordingTime : "Record",
                     replay: "Save " + card.g.replaySeconds + " s",
                     desktop: "Desktop", retroarch: "RetroArch"})[modelData] || spec.label
          }
        }
      }

      Sep { g: card.g; visible: card.g.hasAchievements }

      Column {
        width: parent.width
        visible: card.g.hasAchievements
        spacing: card.g.sized(Style.space(6))
        SectionHead {
          g: card.g
          text: "ACHIEVEMENTS"
          value: card.g.hasAchievements ? (card.g.achievements.unlocked || 0) + " / " + card.g.achievements.total : ""
        }
        ControlRow {
          g: card.g
          key: "achievements"
          width: parent.width
          kind: "meter"
          icon: card.g.icons.achievements
          // The cup draws less ink than the speaker below it.
          iconScale: 1.1
          amount: card.g.hasAchievements ? (card.g.achievements.unlocked || 0) / card.g.achievements.total : 0
        }
      }

      Sep { g: card.g; visible: card.g.volumeAvailable || card.g.outputs.length > 1 }

      Column {
        width: parent.width
        visible: card.g.volumeAvailable || card.g.outputs.length > 1
        spacing: card.g.sized(Style.space(6))
        SectionHead {
          g: card.g
          text: "SOUND"
          value: !card.g.volumeAvailable ? "" : card.g.muted ? "Muted" : Math.round(card.g.volume * 100) + "%"
        }
        ControlRow {
          visible: card.g.volumeAvailable
          g: card.g
          key: "volume"
          width: parent.width
          kind: "slider"
          icon: card.g.muted ? card.g.icons.volumeOff : card.g.icons.volume
          amount: card.g.volume
          dim: card.g.muted
        }
        // The output the game plays through, as the audio panel names its
        // devices; A or left and right switch to the next.
        ControlRow {
          readonly property var spec: card.g.rowSpec("output")
          visible: card.g.outputs.length > 1
          g: card.g
          key: "output"
          width: parent.width
          kind: "text"
          icon: spec.icon
          label: spec.label
          value: (card.g.outputs.indexOf(card.g.currentOutput) + 1) + " / " + card.g.outputs.length
        }
      }

      Sep { g: card.g; visible: !!card.g.game }

      Row {
        id: buttons
        visible: !!card.g.game
        width: parent.width
        spacing: tiles.spacing
        ActionButton {
          g: card.g
          key: "resume"
          width: Math.floor((buttons.width - buttons.spacing) / 2)
          height: card.g.sized(Style.space(44))
          icon: card.g.icons.resume
          iconScale: 1.3
          label: "Resume"
        }
        ActionButton {
          readonly property var spec: card.g.rowSpec("quit")
          g: card.g
          key: "quit"
          urgent: true
          width: Math.floor((buttons.width - buttons.spacing) / 2)
          height: card.g.sized(Style.space(44))
          icon: card.g.icons.quit
          label: spec.label
        }
      }
    }

    AchievementList {
      id: list
      anchors.fill: parent
      visible: card.g.view === "achievements"
      items: card.g.achievementItems
      unlockedCount: card.g.achievements ? (card.g.achievements.unlocked || 0) : 0
      total: card.g.achievements ? (card.g.achievements.total || 0) : 0
      current: card.g.achIndex
      zoom: card.g.zoom
      fontFamily: card.g.fontFamily
      text: card.g.text
      quiet: card.g.quiet
      selectedInk: card.g.selectedInk
      edge: card.g.needsEdge
      edgeColor: card.g.focusEdge
      onHovered: (index, source, mouse) => card.g.hoverAchievement(index, source, mouse)
    }

    ConfirmBlock {
      id: confirm
      g: card.g
      width: parent.width
      visible: card.g.view === "confirm"
    }
  }

  Item { width: 1; height: card.g.sized(Style.space(16)) }

  HintLine {
    g: card.g
    anchors.horizontalCenter: parent.horizontalCenter
  }
}
