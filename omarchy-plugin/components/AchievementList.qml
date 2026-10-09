import QtQuick
import QtQuick.Effects
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The game's achievements inside the card: unlocked, newest first, then locked.
// Each entry is a row in the menu's style: the icon in the row's icon column,
// the name, what it asks for, and when it was earned or how many players have
// it. Names and descriptions wrap rather than cut off. The list scrolls inside
// its own height; the cursor row is always kept in view.
Item {
  id: root

  property var items: []
  property int unlockedCount: 0
  property int total: 0
  property int current: 0
  property real zoom: 1
  property string fontFamily: Style.font.menuFamily
  property color text: Commons.Color.menu.text
  property color quiet: Commons.Color.menu.text
  property color selectedInk: Commons.Color.menu.selectedText
  property bool edge: false
  property color edgeColor: Commons.Color.menu.selectedText
  readonly property int count: list.count

  signal hovered(int index, Item source, var mouse)

  function sized(v) { return v > 0 ? Math.max(1, Math.round(v * root.zoom)) : 0 }

  readonly property var selectedBorderSpec: Border.surfaceSpec("menu", "selected-border", Commons.Color.menu.selectedBorder, 0)
  // Steam's icons are 64 px squares drawn to their edges: slightly larger than
  // the menu's glyphs so they read as pictures, not as ink.
  readonly property int iconSize: root.sized(Style.space(40))
  readonly property int iconRadius: Math.min(root.sized(Style.cornerRadius), root.sized(Style.space(6)))
  readonly property int padX: root.sized(Style.space(8))
  readonly property int padY: root.sized(Style.spacing.rowPaddingX) - root.sized(Style.space(2))

  // Keep the cursor entry whole in view; the first entry brings its heading.
  function reveal() {
    if (list.count === 0) return
    var i = Math.max(0, Math.min(root.current, list.count - 1))
    if (i === 0) list.positionViewAtBeginning()
    else list.positionViewAtIndex(i, ListView.Contain)
  }
  onCurrentChanged: reveal()
  // A list laid out while hidden has guessed heights: place it from the top
  // once shown, or when its entries change.
  function place() {
    list.positionViewAtBeginning()
    reveal()
  }
  onItemsChanged: Qt.callLater(place)
  onVisibleChanged: if (visible) Qt.callLater(place)

  function share(rarity) {
    var r = Number(rarity)
    if (!isFinite(r) || r <= 0) return ""
    return (r < 1 ? "under 1" : String(Math.round(r))) + "% of players"
  }

  ListView {
    id: list
    anchors.fill: parent
    clip: true
    model: root.items
    spacing: root.sized(Style.spacing.xs)
    boundsBehavior: Flickable.StopAtBounds
    currentIndex: root.current
    highlightFollowsCurrentItem: false

    delegate: Item {
      id: slot
      required property var modelData
      required property int index
      // A quiet heading where the unlocked run ends and the locked one starts.
      readonly property bool opensGroup: slot.index === 0
        || !!root.items[slot.index - 1].unlocked !== !!slot.modelData.unlocked
      width: ListView.view.width
      height: (slot.opensGroup ? heading.height : 0) + entry.height

      Item {
        id: heading
        visible: slot.opensGroup
        width: parent.width
        height: slot.opensGroup ? label.implicitHeight + root.sized(Style.spacing.md) * 2 : 0
        Text {
          id: label
          textFormat: Text.PlainText
          x: root.padX
          anchors.verticalCenter: parent.verticalCenter
          text: slot.modelData.unlocked ? "Unlocked  " + root.unlockedCount + " of " + root.total
                                        : "Locked  " + Math.max(0, root.total - root.unlockedCount)
          color: root.quiet
          font.family: root.fontFamily
          font.pixelSize: root.sized(Style.font.body)
        }
      }

      BorderSurface {
        id: entry
        readonly property var modelData: slot.modelData
        readonly property int index: slot.index
        readonly property bool cursor: entry.index === root.current
        readonly property bool earned: !!entry.modelData.unlocked
        readonly property color ink: entry.cursor ? root.selectedInk : root.text

        y: heading.height
        width: parent.width
        height: Math.max(root.iconSize, words.implicitHeight) + root.padY * 2
        radius: root.sized(Style.cornerRadius)
        color: entry.cursor ? Commons.Color.menu.selectedBackground : "transparent"
        borderSpec: !entry.cursor ? Border.none()
          : root.edge ? Border.flat(root.edgeColor, Math.max(1, Style.focusBorderWidth)) : root.selectedBorderSpec

        Item {
          id: art
          x: root.padX
          y: root.padY
          width: root.iconSize
          height: root.iconSize

          Image {
            id: picture
            anchors.fill: parent
            source: entry.modelData.icon || ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            mipmap: true
            sourceSize.width: width * 2
            sourceSize.height: height * 2
            visible: false
          }
          Rectangle {
            id: mask
            anchors.fill: parent
            radius: root.iconRadius
            visible: false
            layer.enabled: true
          }
          // Locked achievements keep their picture, in grey and set back.
          MultiEffect {
            anchors.fill: parent
            source: picture
            visible: picture.status === Image.Ready
            maskEnabled: root.iconRadius > 0
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
            saturation: entry.earned ? 0 : -1
            opacity: entry.earned ? 1 : 0.4
          }
          // No picture: the trophy from the card's icon set, in the icon column.
          InkGlyph {
            anchors.fill: parent
            visible: picture.status !== Image.Ready
            text: "\u{f0538}"
            color: entry.earned ? entry.ink : root.quiet
            size: root.sized(Style.font.iconLarge)
            fontFamily: root.fontFamily
          }
        }

        Column {
          id: words
          x: art.x + art.width + root.sized(Style.space(12))
          width: entry.width - x - root.padX
          // Top-aligned with the picture, as notifications are.
          y: root.padY
          spacing: root.sized(Style.space(2))

          Text {
            width: parent.width
            textFormat: Text.PlainText
            text: entry.modelData.title || "Achievement"
            color: entry.ink
            font.family: root.fontFamily
            font.pixelSize: root.sized(Style.font.body)
            font.weight: Font.Medium
            wrapMode: Text.Wrap
          }
          Text {
            width: parent.width
            textFormat: Text.PlainText
            visible: text.length > 0
            text: entry.modelData.description || (entry.modelData.hidden && !entry.earned ? "Hidden until unlocked" : "")
            color: entry.modelData.description ? entry.ink : root.quiet
            font.family: root.fontFamily
            font.pixelSize: root.sized(Style.font.body)
            wrapMode: Text.Wrap
          }
          Text {
            width: parent.width
            textFormat: Text.PlainText
            visible: text.length > 0
            text: entry.earned
              ? (entry.modelData.when ? "Unlocked " + entry.modelData.when : "Unlocked")
              : root.share(entry.modelData.rarity)
            color: root.quiet
            font.family: root.fontFamily
            font.pixelSize: root.sized(Style.font.body)
            wrapMode: Text.Wrap
          }
        }

        MouseArea {
          id: pointer
          anchors.fill: parent
          hoverEnabled: true
          onPositionChanged: function(mouse) { root.hovered(entry.index, pointer, mouse) }
        }
      }
    }
  }

  Text {
    anchors.centerIn: parent
    visible: list.count === 0
    textFormat: Text.PlainText
    text: "No achievement details yet"
    color: root.quiet
    font.family: root.fontFamily
    font.pixelSize: root.sized(Style.font.body)
  }
}
