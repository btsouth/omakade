pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

Item {
    id: row
    property var colors
    property string family
    property real s: 1
    property real radius: 0
    property bool selected: false
    property string label
    property string detail
    property string icon
    property int order: 0
    property bool compact: false
    signal clicked()
    implicitHeight: (compact ? 46 : 52) * s
    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    RectangularShadow {
        x: 0; y: 12 * row.s; width: 3 * row.s; height: parent.height - 24 * row.s
        radius: width / 2; blur: 14 * row.s
        color: row.tint(row.colors.accent, row.selected ? 0.5 : 0)
    }
    Rectangle {
        x: 0; y: 12 * row.s; width: 3 * row.s; height: parent.height - 24 * row.s
        radius: width / 2; color: row.colors.accent; visible: row.selected
    }
    Text {
        x: 14 * row.s; anchors.verticalCenter: parent.verticalCenter
        text: row.order.toString().padStart(2, "0"); color: row.colors.mutedText
        font.family: row.family; font.pixelSize: 11 * row.s
    }
    GuideIcon {
        x: 44 * row.s; anchors.verticalCenter: parent.verticalCenter
        width: 20 * row.s; height: width; name: row.icon; color: row.colors.fg
    }
    Text {
        x: 80 * row.s; anchors.verticalCenter: parent.verticalCenter
        width: parent.width - x - (value.visible ? value.width + 22 * row.s : 12 * row.s)
        text: row.label; color: row.colors.fg
        font.family: row.family; font.pixelSize: (row.compact ? 16 : 18) * row.s
        font.weight: row.selected ? Font.DemiBold : Font.Normal
        elide: Text.ElideRight
    }
    Text {
        id: value
        anchors.right: parent.right; anchors.rightMargin: 14 * row.s
        anchors.verticalCenter: parent.verticalCenter
        text: row.detail; visible: text !== ""; color: row.colors.mutedText
        font.family: row.family; font.pixelSize: 12 * row.s
    }
    MouseArea { anchors.fill: parent; onClicked: row.clicked() }
}
