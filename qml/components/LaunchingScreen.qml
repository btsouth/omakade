import QtQuick
import QtQuick.Layouts

// Covers the library while a launch hold keeps the launcher's windows out of sight.
// Each held window can be opened here when it needs an answer, such as a sign-in.
Rectangle {
    id: screen
    objectName: "launchingScreen"

    property real uiScale: 1
    property bool couchMode: false
    // The game being started, as the details page shows it: title, cover, backdrop and
    // the accent colours taken from its cover.
    property var game: ({})

    readonly property string title: LaunchHold.title || game.title || "Game"
    readonly property color accentStart: game.accentStart || Theme.accent
    readonly property color accentEnd: game.accentEnd || Theme.blue
    readonly property bool wide: width >= 1180 * uiScale
    // Windows without a title, such as Wine's own, are held but give nothing to pick.
    readonly property var listedWindows: (LaunchHold.windows || []).filter(window => (window.title || "").length > 0)

    color: Theme.darkerBackground
    visible: LaunchHold.active

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    // Who a window belongs to, in the words people use for it.
    function owner(windowClass) {
        const name = windowClass || ""
        if (/^steam$/i.test(name)) return "Steam"
        if (/^steam_app_\d+$/.test(name)) return "Proton"
        if (/\.exe$/i.test(name)) return "Wine"
        return name.length > 0 ? name.charAt(0).toUpperCase() + name.slice(1) : ""
    }

    readonly property var buttons: {
        const list = []
        for (let index = 0; index < windowRepeater.count; ++index) list.push(windowRepeater.itemAt(index))
        list.push(showAllButton)
        return list.filter(button => button !== null)
    }

    // Until someone moves, focus stays on the first window, so a quick A never lets
    // everything go by landing on Show All Windows. With no window yet it rests on the
    // screen itself, and Show All Windows is a deliberate move away.
    property bool moved: false

    function focusFirst() {
        if (windowRepeater.count > 0 && buttons.length > 0) buttons[0].forceActiveFocus(Qt.TabFocusReason)
        else screen.forceActiveFocus(Qt.OtherFocusReason)
    }

    // Up and Down move through the list; Left and Right stay put.
    function navigate(key) {
        moved = true
        const index = buttons.findIndex(button => button.activeFocus)
        if (index < 0) {
            if (key === Qt.Key_Up || key === Qt.Key_Down)
                buttons[key === Qt.Key_Up ? buttons.length - 1 : 0].forceActiveFocus(Qt.TabFocusReason)
            return true
        }
        const step = key === Qt.Key_Up ? -1 : key === Qt.Key_Down ? 1 : 0
        const next = Math.max(0, Math.min(buttons.length - 1, index + step))
        buttons[next].forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    onVisibleChanged: if (visible) { moved = false; Qt.callLater(focusFirst) }
    // A window that opened or closed must not strand focus on a button that went away.
    onButtonsChanged: if (visible && (!moved || !buttons.some(button => button.activeFocus)))
                          Qt.callLater(focusFirst)

    Keys.onUpPressed: event => event.accepted = navigate(Qt.Key_Up)
    Keys.onDownPressed: event => event.accepted = navigate(Qt.Key_Down)
    // Tab stays in the list too, rather than reaching the library underneath.
    Keys.onTabPressed: event => event.accepted = navigate(Qt.Key_Down)
    Keys.onBacktabPressed: event => event.accepted = navigate(Qt.Key_Up)

    // Takes clicks and wheel events so the library underneath stays still.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    // The game's backdrop fills the screen, drifting slowly while it starts. Without one,
    // the cover's own colours do, decoded a dozen pixels wide and scaled back up.
    Item {
        anchors.fill: parent
        clip: true

        Image {
            id: backdrop
            objectName: "launchingBackdrop"
            anchors.centerIn: parent
            width: parent.width
            height: parent.height
            source: screen.game.heroPath || ""
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: Math.ceil(width / 64) * 64
            opacity: status === Image.Ready ? 0.86 : 0
            Behavior on opacity {
                enabled: !Preferences.reducedMotion
                NumberAnimation { duration: 600; easing.type: Easing.OutCubic }
            }
            SequentialAnimation on scale {
                running: screen.visible && backdrop.status === Image.Ready && !Preferences.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 1.06; duration: 24000; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1.06; to: 1.0; duration: 24000; easing.type: Easing.InOutSine }
            }
        }

        Image {
            anchors.fill: parent
            visible: backdrop.status !== Image.Ready
            source: screen.game.coverPath || ""
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 12
            smooth: true
            opacity: status === Image.Ready ? 0.5 : 0
        }

        Rectangle {
            anchors.fill: parent
            opacity: backdrop.status === Image.Ready ? 0.22 : 0.4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: screen.accentStart }
                GradientStop { position: 1.0; color: screen.accentEnd }
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: screen.alpha(Theme.darkerBackground, 0.9) }
                GradientStop { position: 0.42; color: screen.alpha(Theme.darkerBackground, 0.55) }
                GradientStop { position: 1.0; color: screen.alpha(Theme.darkerBackground, 0.1) }
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: screen.alpha(Theme.darkerBackground, 0.25) }
                GradientStop { position: 0.3; color: "transparent" }
                GradientStop { position: 0.62; color: screen.alpha(Theme.darkerBackground, 0.35) }
                GradientStop { position: 1.0; color: screen.alpha(Theme.darkerBackground, 0.95) }
            }
        }
    }

    // Keeps the window list readable over a bright backdrop.
    Rectangle {
        visible: windowRepeater.count > 0
        x: content.x + windowColumn.x - 22 * screen.uiScale
        y: content.y + windowColumn.y - 20 * screen.uiScale
        width: windowColumn.width + 44 * screen.uiScale
        height: windowColumn.height + 40 * screen.uiScale
        radius: Math.max(8, Theme.cornerRadius * 2)
        color: screen.alpha(Theme.darkerBackground, 0.55)
        border.color: screen.alpha(Theme.foreground, 0.08)
    }

    GridLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: (screen.couchMode ? 84 : 36) * screen.uiScale
        anchors.rightMargin: anchors.leftMargin
        anchors.bottomMargin: (screen.couchMode ? 96 : 36) * screen.uiScale
        columns: screen.wide ? 2 : 1
        columnSpacing: 64 * screen.uiScale
        rowSpacing: 28 * screen.uiScale

        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignBottom
            spacing: (screen.couchMode ? 40 : 24) * screen.uiScale

            Rectangle {
                id: coverCard
                readonly property real coverWidth: (screen.couchMode ? 236 : 132) * screen.uiScale
                Layout.preferredWidth: coverWidth
                Layout.preferredHeight: coverWidth * 1.5
                Layout.alignment: Qt.AlignBottom
                radius: Math.max(6, Theme.cornerRadius)

                // A soft shadow from a few widening, fading layers lifts the cover off
                // the backdrop.
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        readonly property real spread: (index + 1) * 7 * screen.uiScale
                        z: -1
                        x: -spread
                        y: -spread + 14 * screen.uiScale
                        width: coverCard.width + spread * 2
                        height: coverCard.height + spread * 2
                        radius: coverCard.radius + spread
                        color: Qt.rgba(0, 0, 0, 0.13 - index * 0.025)
                    }
                }
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: screen.accentStart }
                    GradientStop { position: 1.0; color: screen.accentEnd }
                }

                Item {
                    anchors.fill: parent
                    clip: true
                    CoverArtwork {
                        id: cover
                        anchors.fill: parent
                        source: screen.game.coverPath || ""
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: coverCard.radius
                    color: "transparent"
                    border.color: screen.alpha(Theme.foreground, 0.22)
                }

                Text {
                    visible: cover.status !== Image.Ready
                    anchors.centerIn: parent
                    text: screen.game.coverMark || "◇"
                    color: screen.alpha(Theme.brightForeground, 0.9)
                    font.family: Theme.fontFamily
                    font.pixelSize: coverCard.width * 0.34
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                spacing: 14 * screen.uiScale

                Text {
                    text: "STARTING"
                    color: screen.alpha(Theme.brightForeground, 0.72)
                    font.family: Theme.fontFamily
                    font.pixelSize: 13 * screen.uiScale
                    font.weight: Font.DemiBold
                    font.letterSpacing: 3 * screen.uiScale
                }
                Text {
                    objectName: "launchingTitle"
                    Layout.fillWidth: true
                    text: screen.title
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: (screen.couchMode ? 54 : 30) * screen.uiScale
                    font.weight: Font.Bold
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 1.05
                }

                // An indeterminate bar in the game's colours: something is happening, with
                // no promise of how long. Reduced motion leaves it still.
                Item {
                    id: progress
                    Layout.preferredWidth: Math.min(440 * screen.uiScale, content.width)
                    Layout.preferredHeight: 3 * screen.uiScale
                    Layout.topMargin: 6 * screen.uiScale
                    clip: true

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: screen.alpha(Theme.foreground, 0.14)
                    }
                    Rectangle {
                        id: sweep
                        width: parent.width * (Preferences.reducedMotion ? 1 : 0.32)
                        height: parent.height
                        radius: height / 2
                        opacity: Preferences.reducedMotion ? 0.5 : 1
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: screen.alpha(screen.accentStart, 0) }
                            GradientStop { position: 0.5; color: Qt.lighter(screen.accentStart, 1.3) }
                            GradientStop { position: 1.0; color: screen.accentEnd }
                        }
                        NumberAnimation on x {
                            running: screen.visible && !Preferences.reducedMotion
                            loops: Animation.Infinite
                            from: -sweep.width
                            to: progress.width
                            duration: 1600
                            easing.type: Easing.InOutQuad
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: screen.listedWindows.length > 0
                          ? "Waiting for the game. Open a window here if it needs you, such as a sign-in."
                          : "Waiting for the game to open."
                    color: screen.alpha(Theme.foreground, 0.78)
                    font.family: Theme.fontFamily
                    font.pixelSize: 16 * screen.uiScale
                    wrapMode: Text.Wrap
                }
            }
        }

        ColumnLayout {
            id: windowColumn
            Layout.fillWidth: !screen.wide
            Layout.preferredWidth: screen.wide ? 560 * screen.uiScale : -1
            Layout.alignment: Qt.AlignBottom | Qt.AlignRight
            spacing: 10 * screen.uiScale


            Text {
                visible: windowRepeater.count > 0
                text: windowRepeater.count === 1 ? "1 WINDOW WAITING" : windowRepeater.count + " WINDOWS WAITING"
                color: screen.alpha(Theme.brightForeground, 0.72)
                font.family: Theme.fontFamily
                font.pixelSize: 13 * screen.uiScale
                font.weight: Font.DemiBold
                font.letterSpacing: 3 * screen.uiScale
                Layout.bottomMargin: 4 * screen.uiScale
            }

            Repeater {
                id: windowRepeater
                model: screen.listedWindows
                GlassButton {
                    id: windowButton
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 58 * screen.uiScale
                    objectName: "launchingWindow_" + modelData.address
                    text: modelData.title
                    onClicked: LaunchHold.present(modelData.address)

                    contentItem: RowLayout {
                        spacing: 16 * screen.uiScale
                        Text {
                            Layout.fillWidth: true
                            text: windowButton.text
                            elide: Text.ElideRight
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 16 * screen.uiScale
                            font.weight: Font.Medium
                        }
                        Text {
                            text: screen.owner(windowButton.modelData.windowClass).toUpperCase()
                            visible: text.length > 0
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 12 * screen.uiScale
                            font.letterSpacing: 2 * screen.uiScale
                        }
                    }
                }
            }

            GlassButton {
                id: showAllButton
                objectName: "launchingShowAllButton"
                Layout.alignment: screen.wide ? Qt.AlignRight : Qt.AlignLeft
                Layout.topMargin: 6 * screen.uiScale
                compact: true
                text: "SHOW ALL WINDOWS"
                onClicked: LaunchHold.cancel()
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 54 * screen.uiScale
        anchors.bottomMargin: 20 * screen.uiScale
        spacing: 20 * screen.uiScale
        visible: screen.couchMode

        Repeater {
            model: (windowRepeater.count > 0 ? [{ glyph: Controller.primaryGlyph, label: "OPEN" }] : [])
                   .concat([{ glyph: Controller.backGlyph, label: "SHOW ALL WINDOWS" }])

            Row {
                required property var modelData
                spacing: 7 * screen.uiScale

                Rectangle {
                    width: Math.max(31 * screen.uiScale, glyphText.implicitWidth + 14 * screen.uiScale)
                    height: 31 * screen.uiScale
                    radius: height / 2
                    color: screen.alpha(Theme.foreground, 0.12)
                    border.color: screen.alpha(Theme.foreground, 0.22)

                    Text {
                        id: glyphText
                        anchors.centerIn: parent
                        text: modelData.glyph
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * screen.uiScale
                        font.weight: Font.Bold
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * screen.uiScale
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }
            }
        }
    }
}
