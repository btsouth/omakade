import QtQuick
import QtQuick.Effects
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The game: its cover at a fixed height, as wide as the picture is (an N64
// box, a portrait cover, a Steam header), then the title, where it comes from
// and how long it has been played. With no picture, or no game, a controller
// glyph in a small box takes the cover's place.
Item {
  id: hero
  property var g
  readonly property string cover: g.game ? String(g.game.cover || g.game.banner || "") : ""
  readonly property bool hasArt: art.status === Image.Ready
  readonly property int coverHeight: g.sized(Style.space(112))
  readonly property real aspect: art.implicitHeight > 0 ? art.implicitWidth / art.implicitHeight : 0.75
  readonly property bool truncated: titleText.truncated

  implicitHeight: Math.max(picture.height, labels.implicitHeight)

  Item {
    id: picture
    readonly property int maxWidth: Math.round(hero.width * 0.45)
    readonly property int artWidth: Math.min(maxWidth, Math.round(hero.coverHeight * hero.aspect))
    width: hero.hasArt ? artWidth : hero.g.sized(Style.space(72))
    height: hero.hasArt ? Math.round(artWidth / hero.aspect) : width
    anchors.verticalCenter: parent.verticalCenter

    Image {
      id: art
      anchors.fill: parent
      visible: false
      source: hero.cover
      fillMode: Image.PreserveAspectCrop
      sourceSize.height: hero.coverHeight * 2
      smooth: true
      mipmap: true
    }

    // The theme's radius, at most a small one: a cover is a picture, not a chip.
    Rectangle {
      id: mask
      anchors.fill: parent
      visible: false
      layer.enabled: true
      radius: Math.min(hero.g.sized(Style.cornerRadius), hero.g.sized(Style.space(8)))
    }

    MultiEffect {
      anchors.fill: parent
      visible: hero.hasArt
      source: art
      maskEnabled: mask.radius > 0
      maskSource: mask
      maskThresholdMin: 0.5
      maskSpreadAtMin: 1.0
    }

    // A hairline so a dark cover keeps its edge on a dark card.
    Rectangle {
      anchors.fill: parent
      visible: hero.hasArt
      radius: mask.radius
      color: "transparent"
      border.width: 1
      border.color: Util.alpha(hero.g.text, 0.1)
    }

    Rectangle {
      anchors.fill: parent
      visible: !hero.hasArt
      radius: mask.radius
      color: hero.g.well
      InkGlyph {
        anchors.fill: parent
        text: hero.g.icons.gamepad
        color: hero.g.dim
        size: hero.g.sized(Style.font.display) * 1.3
        fontFamily: hero.g.fontFamily
      }
    }
  }

  Column {
    id: labels
    anchors.left: picture.right
    anchors.leftMargin: hero.g.sized(Style.space(18))
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter

    // A title that would take more than two lines at the large size steps
    // down to the heading size.
    Text {
      id: probe
      visible: false
      width: parent.width
      textFormat: Text.PlainText
      text: titleText.text
      font.family: hero.g.fontFamily
      font.pixelSize: hero.g.sized(Style.font.display)
      font.bold: true
      wrapMode: Text.Wrap
    }

    Text {
      id: titleText
      width: parent.width
      textFormat: Text.PlainText
      text: hero.g.game ? hero.g.game.title : "No game running"
      color: hero.g.text
      font.family: hero.g.fontFamily
      font.pixelSize: hero.g.sized(probe.lineCount > 2 ? Style.font.heading : Style.font.display)
      font.bold: true
      lineHeight: 1.1
      wrapMode: Text.Wrap
      maximumLineCount: 3
      elide: Text.ElideRight
    }

    Item { width: 1; height: hero.g.sized(Style.space(5)); visible: subText.visible }

    Text {
      id: subText
      visible: text !== ""
      width: parent.width
      textFormat: Text.PlainText
      text: hero.g.subline
      color: hero.g.quiet
      font.family: hero.g.fontFamily
      font.pixelSize: hero.g.sized(Style.font.body)
      elide: Text.ElideRight
    }

    Item { width: 1; height: hero.g.sized(Style.space(11)); visible: times.visible }

    // "42 min this session  18 h total": the figure in the text colour.
    Flow {
      id: times
      visible: hero.g.times.length > 0
      width: parent.width
      spacing: hero.g.sized(Style.space(14))
      Repeater {
        model: hero.g.times
        Row {
          required property var modelData
          Text {
            id: figure
            textFormat: Text.PlainText
            text: modelData[0] + " "
            color: hero.g.text
            font.family: hero.g.fontFamily
            font.pixelSize: hero.g.sized(Style.font.subtitle)
            font.weight: Font.DemiBold
          }
          Text {
            textFormat: Text.PlainText
            anchors.baseline: figure.baseline
            text: modelData[1]
            color: hero.g.dim
            font.family: hero.g.fontFamily
            font.pixelSize: hero.g.sized(Style.font.subtitle)
          }
        }
      }
    }
  }
}
