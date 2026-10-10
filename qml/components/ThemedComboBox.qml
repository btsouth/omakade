import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    property real uiScale: 1
    property string availabilityRole: ""
    // Arrows move between controls; with the list open they move through the list.
    property bool controllerNavigation: !popup.visible
    property bool spatialFocusDestination: true
    Keys.onReturnPressed: event => { if (!popup.visible) { popup.open(); event.accepted = true } else event.accepted = false }
    Keys.onEnterPressed: event => { if (!popup.visible) { popup.open(); event.accepted = true } else event.accepted = false }
    property int controllerIndex: -1
    function controllerAccept() {
        if (!popup.visible) {
            popup.open()
        } else if (controllerIndex >= 0 && optionEnabled(controllerIndex)) {
            currentIndex = controllerIndex
            activated(currentIndex)
            popup.close()
        }
    }
    function optionEnabled(index) {
        return !availabilityRole || !!model[index][availabilityRole]
    }
    function controllerNavigate(key) {
        if (!popup.visible) return false
        const step = key === Qt.Key_Up ? -1 : key === Qt.Key_Down ? 1 : 0
        if (step === 0) return true
        let next = controllerIndex + step
        while (next >= 0 && next < count && !optionEnabled(next)) next += step
        if (next >= 0 && next < count) controllerIndex = next
        return true
    }
    implicitWidth: 180 * uiScale
    implicitHeight: 40 * uiScale
    spacing: 8 * uiScale
    font.family: Theme.fontFamily
    font.pixelSize: 13 * uiScale

    contentItem: Text {
        leftPadding: 12 * control.uiScale
        rightPadding: control.indicator.width + 8 * control.uiScale
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.foreground : Qt.rgba(Theme.foreground.r,
                                                            Theme.foreground.g,
                                                            Theme.foreground.b, 0.42)
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.mirrored ? 8 * control.uiScale
                            : control.width - width - 10 * control.uiScale
        y: (control.height - height) / 2
        text: "▼"
        color: control.enabled ? Theme.foreground : Theme.mutedText
        font.family: Theme.fontFamily
        font.pixelSize: 9 * control.uiScale
    }

    background: Rectangle {
        radius: Math.max(5, Theme.cornerRadius)
        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus
                      ? Theme.accent
                      : Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                                Theme.foreground.b, 0.12)
    }

    delegate: ItemDelegate {
        id: optionDelegate
        required property int index
        width: control.width
        height: 40 * control.uiScale
        highlighted: (Controller.driving ? control.controllerIndex : control.highlightedIndex) === index
        enabled: !control.availabilityRole || !!control.model[index][control.availabilityRole]
        opacity: enabled ? 1 : 0.5

        contentItem: Text {
            text: control.textAt(optionDelegate.index)
            font: control.font
            color: Theme.foreground
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: optionDelegate.highlighted
                   ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                   : "transparent"
        }
    }

    popup: Popup {
        onAboutToShow: control.controllerIndex = control.currentIndex
        y: control.height
        width: control.width
        implicitHeight: Math.min(listView.contentHeight, 8 * 40 * control.uiScale) + 2
        padding: 1
        clip: true

        contentItem: ListView {
            id: listView
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: Controller.driving ? control.controllerIndex : control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: Math.max(5, Theme.cornerRadius)
            color: Theme.darkerBackground
            border.color: Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                                  Theme.foreground.b, 0.16)
            border.width: 1
        }
    }
}
