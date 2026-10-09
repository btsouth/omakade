import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "../components"
import "Nav.js" as Nav

// Direction A, "Omarchy panel": one centred card in the language of Omarchy's
// audio, monitor and weather panels. A hero with the game, its readings as
// labelled numbers, a row of action tiles, achievements and sound as panel
// sections, then Resume and Quit as a pair of buttons.
//
// D-pad: the card is a grid of rows. Up and down move between rows (tiles,
// achievements, volume, output, the button pair) and wrap; on a row of several
// items left and right move along it, and moving between rows keeps the
// position across (the two left tiles land on Resume, the two right on Quit).
// On volume, left and right change the level.
Item {
  id: design

  property var g
  readonly property int baseWidth: 440
  readonly property real contentHeight: card.height
  onContentHeightChanged: Qt.callLater(g.fitZoom)

  readonly property var tileKeys: [].concat(["screenshot", "record"], g.replay ? ["replay"] : [], g.game ? ["desktop", "library"] : [])
  readonly property var grid: [
    design.tileKeys,
    g.hasAchievements ? ["achievements"] : [],
    g.volumeAvailable ? ["volume"] : [],
    g.outputs.length > 1 ? ["output"] : [],
    g.game ? ["resume", "quit"] : []
  ].filter(function(r) { return r.length > 0 })

  function navigate(action) {
    var next = Nav.move(grid, g.cursor, action)
    if (next === "") return false
    g.cursor = next
    return true
  }

  BorderSurface {
    id: card
    width: Math.min(g.sized(Style.space(design.baseWidth)), design.width - Style.gapsOut * 2)
    height: card.contentTopInset + content.implicitHeight + card.contentBottomInset
    anchors.horizontalCenter: parent.horizontalCenter
    y: Math.max(Style.gapsOut, Math.round((design.height - card.height) / 2))
    radius: g.sized(Style.cornerRadius)
    color: Commons.Color.menu.background
    borderSpec: Border.surfaceSpec("menu", "border", Commons.Color.menu.border, Math.max(1, Style.space(2)))
    padding: g.sized(Style.spacing.panelPadding)

    MouseArea { anchors.fill: parent }

    Column {
      id: content
      x: card.contentLeftInset
      y: card.contentTopInset
      width: card.width - card.contentLeftInset - card.contentRightInset

      GameHero { g: design.g; width: parent.width }

      Sep { g: design.g; visible: design.g.readouts.length > 0 }

      // The readings, one column each, centred over the tiles below.
      Row {
        id: readings
        visible: design.g.readouts.length > 0
        width: parent.width
        spacing: tiles.spacing
        Repeater {
          model: design.g.readouts
          Readout {
            required property var modelData
            g: design.g
            width: Math.floor((readings.width - readings.spacing * (design.g.readouts.length - 1)) / design.g.readouts.length)
            centered: true
            label: modelData.label
            value: modelData.value
            unit: modelData.unit
          }
        }
      }

      Sep { g: design.g }

      Item {
        id: body
        width: parent.width
        height: design.g.view === "confirm" ? confirm.implicitHeight : main.implicitHeight

        Column {
          id: main
          width: parent.width
          visible: design.g.view === "main"

          Row {
            id: tiles
            width: parent.width
            spacing: design.g.sized(Style.space(8))
            Repeater {
              model: design.tileKeys
              Tile {
                required property string modelData
                readonly property var spec: design.g.rowSpec(modelData)
                g: design.g
                key: modelData
                width: Math.floor((tiles.width - tiles.spacing * (design.tileKeys.length - 1)) / design.tileKeys.length)
                height: design.g.sized(Style.space(72))
                icon: modelData === "record" && design.g.recording ? design.g.icons.stop : spec.icon
                iconScale: spec.iconScale || 1
                iconColor: spec.iconColor !== undefined ? spec.iconColor : ink
                label: ({screenshot: "Screenshot", record: design.g.recording ? "Stop " + design.g.recordingTime : "Record",
                         replay: "Save " + ((design.g.replay && design.g.replay.seconds) || 30) + " s",
                         desktop: "Desktop", library: "Library"})[modelData]
              }
            }
          }

          Sep { g: design.g; visible: design.g.hasAchievements }

          Column {
            width: parent.width
            visible: design.g.hasAchievements
            spacing: design.g.sized(Style.space(6))
            SectionHead {
              g: design.g
              text: "ACHIEVEMENTS"
              value: design.g.hasAchievements ? design.g.achievements.unlocked + " / " + design.g.achievements.total : ""
            }
            ControlRow {
              g: design.g
              key: "achievements"
              width: parent.width
              kind: "meter"
              icon: design.g.icons.achievements
              amount: design.g.hasAchievements ? design.g.achievements.unlocked / design.g.achievements.total : 0
            }
          }

          Sep { g: design.g; visible: design.g.volumeAvailable }

          Column {
            width: parent.width
            visible: design.g.volumeAvailable
            spacing: design.g.sized(Style.space(6))
            SectionHead {
              g: design.g
              text: "SOUND"
              value: design.g.muted ? "Muted" : Math.round(design.g.volume * 100) + "%"
            }
            ControlRow {
              g: design.g
              key: "volume"
              width: parent.width
              kind: "slider"
              icon: design.g.muted ? design.g.icons.volumeOff : design.g.icons.volume
              amount: design.g.volume
              dim: design.g.muted
            }
            ControlRow {
              readonly property var spec: design.g.rowSpec("output")
              visible: design.g.outputs.length > 1
              g: design.g
              key: "output"
              width: parent.width
              kind: "text"
              icon: spec.icon
              label: spec.label
              value: spec.value || ""
            }
          }

          Sep { g: design.g; visible: !!design.g.game }

          Row {
            id: buttons
            visible: !!design.g.game
            width: parent.width
            spacing: tiles.spacing
            ActionButton {
              g: design.g
              key: "resume"
              width: Math.floor((buttons.width - buttons.spacing) / 2)
              height: design.g.sized(Style.space(44))
              icon: design.g.icons.resume
              iconScale: 1.3
              label: "Resume"
            }
            ActionButton {
              readonly property var spec: design.g.rowSpec("quit")
              g: design.g
              key: "quit"
              urgent: true
              width: Math.floor((buttons.width - buttons.spacing) / 2)
              height: design.g.sized(Style.space(44))
              icon: design.g.icons.quit
              label: spec.label
            }
          }
        }

        AchievementList {
          anchors.fill: parent
          visible: design.g.view === "achievements"
          items: design.g.achievementItems
          unlockedCount: design.g.achievements ? (design.g.achievements.unlocked || 0) : 0
          total: design.g.achievements ? (design.g.achievements.total || 0) : 0
          current: design.g.achIndex
          zoom: design.g.zoom
          fontFamily: design.g.fontFamily
          text: design.g.text
          quiet: design.g.quiet
          selectedInk: design.g.selectedInk
          edge: design.g.needsEdge
          edgeColor: design.g.focusEdge
        }

        ConfirmBlock {
          id: confirm
          g: design.g
          width: parent.width
          visible: design.g.view === "confirm"
        }
      }

      Item { width: 1; height: design.g.sized(Style.space(16)) }

      HintLine {
        g: design.g
        anchors.horizontalCenter: parent.horizontalCenter
      }
    }
  }
}
