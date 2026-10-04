pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

// Large purposeful actions, rather than a material tile for every setting row.
GlassSurface {
    id: tile
    framed: false
    shadowed: false
    property bool setting: false
    property string family
    property string label
    property string detail
    property string icon
    property int order: 0
    property bool selected: false
    property bool primary: false
    property real s: scaleFactor
    signal clicked()
    RectangularShadow {
        x: 2 * tile.s; y: 16 * tile.s; width: 3 * tile.s; height: parent.height - 32 * tile.s
        radius: width / 2; blur: 24 * tile.s
        color: tile.tint(tile.colors.accent, tile.selected ? 0.6 : 0)
    }
    Rectangle {
        x: 2 * tile.s; y: 16 * tile.s; width: 3 * tile.s; height: parent.height - 32 * tile.s
        radius: width / 2; color: tile.colors.accent; visible: tile.selected
    }
    GuideIcon {
        x: 22 * tile.s; y: tile.primary ? (tile.height - height) / 2 : 14 * tile.s
        width: (tile.primary ? 28 : 22) * tile.s; height: width
        name: tile.icon; color: tile.colors.accent; visible: !tile.setting
    }
    Rectangle {
        anchors.fill: parent; radius: tile.radius; color: "transparent"
        border.width: 1; border.color: tile.tint(tile.selected ? tile.colors.accent : tile.colors.brightFg, tile.selected ? 0.55 : 0.10)
    }
    Text {
        x: (tile.primary ? 76 : 22) * tile.s; y: (tile.primary ? 10 : tile.setting ? 14 : 41) * tile.s
        width: parent.width - x - 20 * tile.s
        text: tile.label; color: tile.colors.brightFg; elide: Text.ElideRight
        font.family: tile.family; font.pixelSize: (tile.primary ? 25 : tile.setting ? 14 : 18) * tile.s
        font.weight: tile.selected ? Font.DemiBold : Font.Medium
    }
    Text {
        x: (tile.primary ? 76 : 22) * tile.s; y: (tile.primary ? 43 : tile.setting ? 45 : 65) * tile.s
        width: parent.width - x - 20 * tile.s
        text: tile.detail; elide: Text.ElideRight
        font.family: tile.family; font.pixelSize: (tile.setting ? 25 : 12) * tile.s
        color: tile.setting ? tile.colors.brightFg : tile.colors.mutedText
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 20 * tile.s; y: 15 * tile.s
        text: tile.order.toString().padStart(2, "0"); color: tile.colors.mutedText
        font.family: tile.family; font.pixelSize: 11 * tile.s
    }
    MouseArea { anchors.fill: parent; onClicked: tile.clicked() }
}
