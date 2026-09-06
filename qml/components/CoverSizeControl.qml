import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property bool couch: false
    property real uiScale: 1
    readonly property int percent: couch ? Preferences.couchCoverSize : Preferences.coverSize
    signal editingFinished()
    function focusSlider() { sizeSlider.forceActiveFocus(Qt.TabFocusReason) }
    function apply(value) {
        if (couch) Preferences.couchCoverSize = Math.round(value)
        else Preferences.coverSize = Math.round(value)
    }
    spacing: 10
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: root.couch ? "COUCH GRID COVER SIZE" : "COVER SIZE"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 11 * root.uiScale }
        GlassButton { id: resetSize; property Item controllerDownTarget: sizeSlider; compact: true; text: "RESET"; enabled: root.percent !== 100; onClicked: root.apply(100) }
    }
    Slider {
        id: sizeSlider
        objectName: root.couch ? "couchCoverSizeSlider" : "coverSizeSlider"
        property bool controllerNavigation: false
        Layout.fillWidth: true
        from: 60; to: 160; stepSize: 10; value: root.percent
        snapMode: Slider.SnapAlways
        Accessible.name: root.couch ? "Couch grid cover size" : "Library cover size"
        onMoved: root.apply(value)
        Keys.onUpPressed: event => { if (resetSize.enabled) resetSize.forceActiveFocus(Qt.TabFocusReason); event.accepted = true }
        Keys.onReturnPressed: root.editingFinished()
        Keys.onEnterPressed: root.editingFinished()
        background: Rectangle {
            x: sizeSlider.leftPadding
            y: sizeSlider.topPadding + sizeSlider.availableHeight / 2 - height / 2
            width: sizeSlider.availableWidth; height: 4 * root.uiScale; radius: height / 2
            color: Theme.mutedText
            Rectangle { width: sizeSlider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: Theme.accent }
        }
        handle: Rectangle {
            x: sizeSlider.leftPadding + sizeSlider.visualPosition * (sizeSlider.availableWidth - width)
            y: sizeSlider.topPadding + sizeSlider.availableHeight / 2 - height / 2
            implicitWidth: 20 * root.uiScale; implicitHeight: 20 * root.uiScale; radius: width / 2
            color: Theme.accent; border.color: sizeSlider.activeFocus ? Theme.brightForeground : Theme.accent; border.width: sizeSlider.activeFocus ? 2 : 1
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "SMALLER"; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale }
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: root.percent + "%"; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale }
        Text { text: "LARGER"; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale }
    }
}
