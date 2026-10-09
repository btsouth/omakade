import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "../components"
import "Nav.js" as Nav

// Direction B, "Quick access sheet": a full-height Omarchy panel docked to the
// right edge of the game's screen. The game stays in view beside it, without
// a scrim. The hero and readings head the sheet, the actions and the panel
// sections follow, and Quit sits apart at the foot.
//
// D-pad: one column, top to bottom (Resume, Return to desktop, Game library,
// the capture pair, achievements, volume, output, Quit), wrapping at the ends.
// Left and right move between Screenshot and Record, and set the level on
// volume.
Item {
  id: design

  property var g
  readonly property int baseWidth: 400
  readonly property color scrim: "transparent"
  readonly property real contentHeight: sheet.contentTopInset + top.implicitHeight + design.g.sized(Style.space(28)) + foot.implicitHeight + sheet.contentBottomInset
  onContentHeightChanged: Qt.callLater(g.fitZoom)

  readonly property var grid: [
    g.game ? ["resume"] : [], g.game ? ["desktop"] : [], g.game ? ["library"] : [],
    ["screenshot", "record"], g.replay ? ["replay"] : [],
    g.hasAchievements ? ["achievements"] : [],
    g.volumeAvailable ? ["volume"] : [],
    g.outputs.length > 1 ? ["output"] : [],
    g.game ? ["quit"] : []
  ].filter(function(r) { return r.length > 0 })

  function navigate(action) {
    var next = Nav.move(grid, g.cursor, action)
    if (next === "") return false
    g.cursor = next
    return true
  }

  BorderSurface {
    id: sheet
    width: Math.min(g.sized(Style.space(design.baseWidth)), design.width - Style.gapsOut * 2)
    height: design.height - Style.gapsOut * 2
    x: design.width - width - Style.gapsOut
    y: Style.gapsOut
    radius: g.sized(Style.cornerRadius)
    color: Commons.Color.menu.background
    borderSpec: Border.surfaceSpec("menu", "border", Commons.Color.menu.border, Math.max(1, Style.space(2)))
    padding: g.sized(Style.spacing.panelPadding)

    MouseArea { anchors.fill: parent }

    Column {
      id: top
      x: sheet.contentLeftInset
      y: sheet.contentTopInset
      width: sheet.width - sheet.contentLeftInset - sheet.contentRightInset

      GameHero { g: design.g; width: parent.width }

      Sep { g: design.g; visible: design.g.readouts.length > 0 }

      // The readings in two columns, as the network panel lists its figures.
      Grid {
        id: readings
        visible: design.g.readouts.length > 0
        width: parent.width
        columns: 2
        rowSpacing: design.g.sized(Style.space(12))
        columnSpacing: design.g.sized(Style.space(12))
        Repeater {
          model: design.g.readouts
          Readout {
            required property var modelData
            g: design.g
            width: Math.floor((readings.width - readings.columnSpacing) / 2)
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
          spacing: design.g.sized(Style.space(4))

          Repeater {
            model: design.g.game ? ["resume", "desktop", "library"] : []
            ActionButton {
              required property string modelData
              readonly property var spec: design.g.rowSpec(modelData)
              g: design.g
              key: modelData
              bordered: false
              leftAlign: true
              width: main.width
              height: design.g.sized(Style.space(44))
              icon: spec.icon
              iconScale: spec.iconScale || 1
              label: spec.label
            }
          }

          Sep { g: design.g; gap: design.g.sized(Style.space(10)) }

          SectionHead { g: design.g; text: "CAPTURE"; value: design.g.recording ? "REC " + design.g.recordingTime : "" }
          Item { width: 1; height: design.g.sized(Style.space(2)) }

          Row {
            id: capture
            width: parent.width
            spacing: design.g.sized(Style.space(8))
            ActionButton {
              g: design.g
              key: "screenshot"
              width: Math.floor((capture.width - capture.spacing) / 2)
              height: design.g.sized(Style.space(44))
              icon: design.g.icons.screenshot
              label: "Screenshot"
            }
            ActionButton {
              g: design.g
              key: "record"
              width: Math.floor((capture.width - capture.spacing) / 2)
              height: design.g.sized(Style.space(44))
              icon: design.g.recording ? design.g.icons.stop : design.g.icons.record
              iconColor: design.g.recording ? design.g.recordingInk : ink
              label: design.g.recording ? "Stop" : "Record"
            }
          }
          ActionButton {
            visible: !!design.g.replay
            g: design.g
            key: "replay"
            width: parent.width
            height: design.g.sized(Style.space(44))
            icon: design.g.icons.replay
            label: "Save last " + ((design.g.replay && design.g.replay.seconds) || 30) + " s"
          }

          Sep { g: design.g; gap: design.g.sized(Style.space(10)); visible: design.g.hasAchievements }

          SectionHead {
            visible: design.g.hasAchievements
            g: design.g
            text: "ACHIEVEMENTS"
            value: design.g.hasAchievements ? design.g.achievements.unlocked + " / " + design.g.achievements.total : ""
          }
          ControlRow {
            visible: design.g.hasAchievements
            g: design.g
            key: "achievements"
            width: parent.width
            kind: "meter"
            icon: design.g.icons.achievements
            amount: design.g.hasAchievements ? design.g.achievements.unlocked / design.g.achievements.total : 0
          }

          Sep { g: design.g; gap: design.g.sized(Style.space(10)); visible: design.g.volumeAvailable }

          SectionHead {
            visible: design.g.volumeAvailable
            g: design.g
            text: "SOUND"
            value: design.g.muted ? "Muted" : Math.round(design.g.volume * 100) + "%"
          }
          ControlRow {
            visible: design.g.volumeAvailable
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
          stacked: true
          visible: design.g.view === "confirm"
        }
      }
    }

    // Quit apart at the foot of the sheet, then what the buttons do.
    Column {
      id: foot
      x: sheet.contentLeftInset
      y: sheet.height - sheet.contentBottomInset - height
      width: sheet.width - sheet.contentLeftInset - sheet.contentRightInset

      Sep { g: design.g; visible: !!design.g.game && design.g.view === "main" }

      ActionButton {
        readonly property var spec: design.g.rowSpec("quit")
        visible: !!design.g.game && design.g.view === "main"
        g: design.g
        key: "quit"
        urgent: true
        bordered: false
        leftAlign: true
        width: parent.width
        height: design.g.sized(Style.space(44))
        icon: design.g.icons.quit
        label: spec.label
      }

      Item { width: 1; height: design.g.sized(Style.space(16)) }

      HintLine { g: design.g }
    }
  }
}
