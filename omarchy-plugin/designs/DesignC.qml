import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "../components"
import "Nav.js" as Nav

// Direction C, "Two-column dashboard": one wider centred card. The hero runs
// across the top; below it the actions on the left in the menu's rows, and on
// the right the game's state as panel sections: readings, achievements with
// the latest unlock, and sound.
//
// D-pad: up and down move within a column and wrap there. Right from any
// action goes to the nearest control on the right (achievements, volume,
// output); left from achievements goes back to the action it came from. On
// volume and output, left and right set the level and the device, as in
// Omarchy's audio panel; up to achievements, then left, leads back.
Item {
  id: design

  property var g
  readonly property int baseWidth: 720
  readonly property real contentHeight: card.height
  onContentHeightChanged: Qt.callLater(g.fitZoom)

  readonly property var actions: (g.game ? ["resume", "desktop", "library"] : [])
    .concat(["screenshot", "record"], g.replay ? ["replay"] : [], g.game ? ["quit"] : [])
  readonly property var controls: (g.hasAchievements ? ["achievements"] : [])
    .concat(g.volumeAvailable ? ["volume"] : [], g.outputs.length > 1 ? ["output"] : [])
  property string lastAction: "resume"

  function navigate(action) {
    var inLeft = actions.indexOf(g.cursor) >= 0
    var list = inLeft ? actions : controls
    var i = list.indexOf(g.cursor)
    if (i < 0) { g.cursor = actions[0]; return true }
    if (action === "up" || action === "down") {
      g.cursor = list[(i + (action === "up" ? -1 : 1) + list.length) % list.length]
      if (inLeft) design.lastAction = g.cursor
      return true
    }
    if (!inLeft && g.cursor !== "achievements") return false
    if (inLeft && action === "right" && controls.length) {
      design.lastAction = g.cursor
      g.cursor = controls[Math.min(controls.length - 1, Math.floor(i * controls.length / actions.length))]
    } else if (!inLeft && action === "left") {
      g.cursor = actions.indexOf(design.lastAction) >= 0 ? design.lastAction : actions[0]
    }
    return true
  }

  readonly property var latest: {
    var items = g.achievementItems || []
    for (var i = 0; i < items.length; i++) if (items[i].unlocked) return items[i]
    return null
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

      Sep { g: design.g }

      Item {
        id: columns
        width: parent.width
        readonly property int gap: design.g.sized(Style.space(18))
        readonly property int leftWidth: Math.round((width - gap * 2 - 1) * 0.46)
        height: design.g.view === "confirm" ? confirm.implicitHeight : Math.max(actionsCol.implicitHeight, stateCol.implicitHeight)

        // ---- Actions
        Column {
          id: actionsCol
          width: columns.leftWidth
          visible: design.g.view !== "confirm"
          spacing: design.g.sized(Style.space(4))

          Repeater {
            model: design.actions
            Column {
              id: slot
              required property string modelData
              required property int index
              readonly property var spec: design.g.rowSpec(modelData)
              width: actionsCol.width
              // A rule before capture and before Quit.
              Sep {
                g: design.g
                gap: design.g.sized(Style.space(6))
                visible: slot.index > 0 && (slot.modelData === "screenshot" || slot.modelData === "quit")
              }
              ActionButton {
                g: design.g
                key: slot.modelData
                bordered: false
                leftAlign: true
                urgent: slot.modelData === "quit"
                width: actionsCol.width
                height: design.g.sized(Style.space(42))
                icon: slot.modelData === "record" && design.g.recording ? design.g.icons.stop : slot.spec.icon
                iconScale: slot.spec.iconScale || 1
                iconColor: slot.spec.iconColor !== undefined ? slot.spec.iconColor : ink
                label: slot.spec.label
                value: slot.spec.value || ""
              }
            }
          }
        }

        Rectangle {
          visible: design.g.view !== "confirm"
          x: columns.leftWidth + columns.gap
          width: 1
          height: parent.height
          color: Qt.rgba(design.g.text.r, design.g.text.g, design.g.text.b, 0.12)
        }

        // ---- The game's state
        Item {
          x: columns.leftWidth + columns.gap * 2 + 1
          width: columns.width - x
          height: parent.height
          visible: design.g.view !== "confirm"

          Column {
            id: stateCol
            width: parent.width
            visible: design.g.view === "main"

            Item { width: 1; height: design.g.sized(Style.space(4)) }


            Grid {
              id: readings
              visible: design.g.readouts.length > 0
              width: parent.width
              columns: 4
              columnSpacing: design.g.sized(Style.space(8))
              Repeater {
                model: design.g.readouts
                Readout {
                  required property var modelData
                  g: design.g
                  width: Math.floor((readings.width - readings.columnSpacing * 3) / 4)
                  label: modelData.label
                  value: modelData.value
                  unit: modelData.unit
                }
              }
            }

            Sep { g: design.g; visible: design.g.hasAchievements && design.g.readouts.length > 0 }

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
              Row {
                visible: !!design.latest
                width: parent.width
                spacing: design.g.sized(Style.space(8))
                Caps { g: design.g; text: "LATEST"; anchors.verticalCenter: parent.verticalCenter }
                Text {
                  anchors.verticalCenter: parent.verticalCenter
                  width: parent.width - x
                  textFormat: Text.PlainText
                  text: design.latest ? design.latest.title : ""
                  color: design.g.text
                  font.family: design.g.fontFamily
                  font.pixelSize: design.g.sized(Style.font.body)
                  elide: Text.ElideRight
                }
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
        }

        ConfirmBlock {
          id: confirm
          g: design.g
          width: columns.leftWidth * 1.4
          anchors.horizontalCenter: parent.horizontalCenter
          visible: design.g.view === "confirm"
        }
      }

      Sep { g: design.g }

      HintLine { g: design.g }
    }
  }
}
