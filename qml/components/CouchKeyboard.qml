import QtQuick
import QtQuick.Layouts

FocusScope {
    id: root

    property string value: ""
    property string title: "SEARCH YOUR LIBRARY"
    property string placeholder: "Start typing"
    property bool passwordMode: false
    property int maximumLength: 128
    property string keyboardMode: "upper"
    property string gridObjectName: "couchKeyboardGrid"
    readonly property int columns: 10
    readonly property real uiScale: Math.max(1, Math.min(2,
                                                         Math.min(width / 1920,
                                                                  height / 1080)))
    readonly property var upperKeys: [
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
        "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
        "A", "S", "D", "F", "G", "H", "J", "K", "L", "'",
        "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/",
        "BACKSPACE", "SPACE", "CLEAR", "SHIFT", "SYMBOLS", "DONE"
    ]
    readonly property var lowerKeys: upperKeys.map(key => key.length === 1 ? key.toLowerCase() : key)
    readonly property var symbolKeys: [
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
        "!", "@", "#", "$", "%", "^", "&", "*", "(", ")",
        "-", "_", "=", "+", "[", "]", "{", "}", ";", ":",
        "'", "\"", "<", ">", "?", "/", "\\", "|", "~", "`",
        "BACKSPACE", "SPACE", "CLEAR", "LETTERS", "SHIFT", "DONE"
    ]
    readonly property var keys: keyboardMode === "symbols" ? symbolKeys
                                : keyboardMode === "lower" ? lowerKeys : upperKeys

    signal valueEdited(string value)
    signal accepted(string value)
    signal canceled()

    Accessible.name: "On-screen keyboard"
    Accessible.role: Accessible.Pane

    function alpha(color, amount) {
        return Qt.rgba(color.r, color.g, color.b, amount)
    }

    function focusKeyboard() {
        keyGrid.currentIndex = keyboardMode === "symbols" ? 0 : 20
        keyGrid.forceActiveFocus(Qt.TabFocusReason)
    }

    function appendText(text) {
        if (text.length > 0 && value.length < maximumLength) {
            value += text.slice(0, maximumLength - value.length)
            valueEdited(value)
        }
    }

    function displayValue() {
        if (!passwordMode) {
            return value
        }
        let masked = ""
        for (let index = 0; index < value.length; ++index) {
            masked += "•"
        }
        return masked
    }

    function activateKey(index) {
        if (index < 0 || index >= keys.length) {
            return
        }
        const key = keys[index]
        if (key === "BACKSPACE") {
            value = value.slice(0, -1)
        } else if (key === "SPACE") {
            appendText(" ")
            return
        } else if (key === "CLEAR") {
            value = ""
        } else if (key === "DONE") {
            accepted(value)
            return
        } else if (key === "SHIFT") {
            keyboardMode = keyboardMode === "lower" ? "upper" : "lower"
        } else if (key === "SYMBOLS") {
            keyboardMode = "symbols"
        } else if (key === "LETTERS") {
            keyboardMode = "upper"
        } else {
            appendText(key)
            return
        }
        valueEdited(value)
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Backspace) {
            root.value = root.value.slice(0, -1)
            root.valueEdited(root.value)
            event.accepted = true
        } else if (event.text.length > 0 && event.text.charCodeAt(0) >= 32) {
            root.appendText(event.text)
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.alpha(Theme.darkerBackground, 0.92)

        MouseArea { anchors.fill: parent; onClicked: root.canceled() }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 96 * root.uiScale, 1180 * root.uiScale)
        height: Math.min(parent.height - 96 * root.uiScale, 660 * root.uiScale)
        radius: Math.max(14 * root.uiScale, Theme.cornerRadius * 2)
        color: root.alpha(Theme.background, 0.98)
        border.width: 1
        border.color: root.alpha(Theme.foreground, 0.18)
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 30 * root.uiScale
            spacing: 16 * root.uiScale

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3 * root.uiScale

                    Text {
                        text: root.title
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 26 * root.uiScale
                        font.weight: Font.Bold
                    }
                    Text {
                        text: "Use the directional pad and confirm button."
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 13 * root.uiScale
                    }
                }

            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60 * root.uiScale
                radius: Math.max(8 * root.uiScale, Theme.cornerRadius)
                color: root.alpha(Theme.foreground, 0.07)
                border.width: 1
                border.color: root.alpha(Theme.foreground, 0.25)

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 22 * root.uiScale
                    anchors.rightMargin: 22 * root.uiScale
                    verticalAlignment: Text.AlignVCenter
                    text: root.value.length > 0
                          ? root.displayValue()
                          : root.placeholder
                    textFormat: Text.PlainText
                    color: root.value.length > 0 ? Theme.brightForeground : Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 23 * root.uiScale
                    elide: Text.ElideRight
                }
            }

            FocusScope {
                id: keyGrid
                objectName: root.gridObjectName
                Layout.fillWidth: true
                Layout.fillHeight: true
                property int currentIndex: 20
                readonly property int count: root.keys.length
                function tile(index) {
                    return index < 40 ? letterTiles.itemAt(index) : actionTiles.itemAt(index - 40)
                }
                function moveVertical(delta) {
                    const row = Math.floor(currentIndex / root.columns)
                    const nextRow = row + delta
                    if (nextRow < 0 || nextRow > 4) return
                    const current = tile(currentIndex)
                    if (!current) return
                    const x = current.mapToItem(keyGrid, current.width / 2, 0).x
                    let best = currentIndex
                    let distance = Number.MAX_VALUE
                    for (let index = nextRow * root.columns; index < Math.min((nextRow + 1) * root.columns, count); ++index) {
                        const target = tile(index)
                        if (!target) continue
                        const dx = Math.abs(target.mapToItem(keyGrid, target.width / 2, 0).x - x)
                        if (dx < distance) { distance = dx; best = index }
                    }
                    currentIndex = best
                }

                function navigateLeftInput(event) {
                    if (currentIndex % root.columns > 0) {
                        currentIndex--
                    }
                    event.accepted = true
                }
                function controllerLeft() { const action = { accepted: false }; navigateLeftInput(action); return action.accepted }
                Keys.onLeftPressed: event => navigateLeftInput(event)

                function navigateRightInput(event) {
                    if (currentIndex % root.columns < root.columns - 1
                            && currentIndex + 1 < count) {
                        currentIndex++
                    }
                    event.accepted = true
                }
                function controllerRight() { const action = { accepted: false }; navigateRightInput(action); return action.accepted }
                Keys.onRightPressed: event => navigateRightInput(event)

                function navigateUpInput(event) { moveVertical(-1); event.accepted = true }
                function controllerUp() { const action = { accepted: false }; navigateUpInput(action); return action.accepted }
                Keys.onUpPressed: event => navigateUpInput(event)

                function navigateDownInput(event) { moveVertical(1); event.accepted = true }
                function controllerDown() { const action = { accepted: false }; navigateDownInput(action); return action.accepted }
                Keys.onDownPressed: event => navigateDownInput(event)
                Keys.onTabPressed: function(event) {
                    currentIndex = (currentIndex + 1) % count
                    event.accepted = true
                }
                Keys.onBacktabPressed: function(event) {
                    currentIndex = (currentIndex + count - 1) % count
                    event.accepted = true
                }

                function acceptInput(event) {
                    root.activateKey(currentIndex)
                    event.accepted = true
                }
                function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
                Keys.onReturnPressed: event => acceptInput(event)
                Keys.onEnterPressed: function(event) {
                    root.activateKey(currentIndex)
                    event.accepted = true
                }
                Keys.onEscapePressed: function(event) {
                    root.canceled()
                    event.accepted = true
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 16 * root.uiScale
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: root.columns
                        rowSpacing: 8 * root.uiScale
                        columnSpacing: 8 * root.uiScale
                        Repeater {
                            id: letterTiles
                            model: 40
                            KeyTile { required property int index; keyIndex: index }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 62 * root.uiScale
                        spacing: 8 * root.uiScale
                        Repeater {
                            id: actionTiles
                            model: root.keys.length - 40
                            KeyTile {
                                required property int index
                                keyIndex: 40 + index
                                Layout.preferredWidth: (keyIndex === 41 ? 3 : keyIndex === 45 ? 1.5 : 1) * 80
                            }
                        }
                    }
                }
                component KeyTile: Rectangle {
                    required property int keyIndex
                    readonly property string key: root.keys[keyIndex]
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    implicitWidth: 80
                    implicitHeight: 58 * root.uiScale
                    radius: Math.max(7 * root.uiScale, Theme.cornerRadius)
                    color: root.alpha(key === "DONE" || keyGrid.currentIndex === keyIndex ? Theme.accent : Theme.foreground,
                                      key === "DONE" || keyGrid.currentIndex === keyIndex ? 0.20 : 0.055)
                    border.width: keyGrid.currentIndex === keyIndex ? 3 : 1
                    border.color: keyGrid.currentIndex === keyIndex ? Theme.accent : root.alpha(Theme.foreground, 0.16)
                    Text {
                        anchors.centerIn: parent
                        text: key === "BACKSPACE" ? "DELETE" : key
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: (key.length > 1 ? 13 : 22) * root.uiScale
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            keyGrid.currentIndex = keyIndex
                            keyGrid.forceActiveFocus(Qt.MouseFocusReason)
                            root.activateKey(keyIndex)
                        }
                    }
                }
            }

            Row {
                Layout.alignment: Qt.AlignRight
                spacing: 18 * root.uiScale

                Text {
                    text: Controller.primaryGlyph + "  SELECT"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * root.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    text: Controller.favoriteGlyph + "  DELETE     " + Controller.toolbarGlyph + "  SPACE     " + Controller.backGlyph + "  CANCEL     START  DONE"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * root.uiScale
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
