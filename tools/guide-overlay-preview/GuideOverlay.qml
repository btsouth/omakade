import QtQuick
import QtQuick.Effects

// The in-game guide. Opened with the controller's Guide button over a running game:
// a frosted still of the game, the game's card, quick actions and the session's status.
// Every color comes from the Omarchy theme and cross-fades when the theme changes.
FocusScope {
    id: guide

    property var theme
    property var game: ({})
    property url backdrop
    property bool shown: false
    property real volume: 0.72
    property string clock: "21:47"
    property string date: "Sunday, October 4"

    signal actionTriggered(string action)
    signal closeRequested()

    readonly property real s: height / 1080
    readonly property real radius: Math.max(0, theme ? theme.cornerRadius : 0) * s

    // Theme colors, animated so a theme switch cross-fades instead of snapping.
    QtObject {
        id: pal
        property color bg: guide.theme.background
        property color darkBg: guide.theme.darkBackground
        property color darkerBg: guide.theme.darkerBackground
        property color lighterBg: guide.theme.lighterBackground
        property color selection: guide.theme.selection
        property color accent: guide.theme.accent
        property color fg: guide.theme.foreground
        property color brightFg: guide.theme.brightForeground
        property color mutedText: guide.theme.mutedText
        property color red: guide.theme.red
        property color green: guide.theme.green
        property color yellow: guide.theme.yellow
        Behavior on bg { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on darkBg { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on darkerBg { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on lighterBg { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on selection { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on accent { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on fg { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on brightFg { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on mutedText { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on red { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on green { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        Behavior on yellow { ColorAnimation { duration: 450; easing.type: Easing.InOutQuad } }
    }
    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    // ---- Legibility ---------------------------------------------------------------------
    // Glass is the blurred game, dimmed toward the theme's darkest color (the base), under a
    // tint of the theme's background. The tint is as clear as the theme allows: the least
    // opaque one at which, over a black frame and a white one alike, body text keeps 7:1 (or
    // 90% of its contrast on the theme's own background), muted text 4.5:1 (or 85%) and the
    // accent 3:1 (or 85%).
    readonly property real glassBase: 0.6
    function luminance(c) {
        const f = v => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
        return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b)
    }
    function contrast(a, b) {
        const x = luminance(a), y = luminance(b)
        return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05)
    }
    function over(c, alpha, base) {
        return Qt.rgba(c.r * alpha + base.r * (1 - alpha), c.g * alpha + base.g * (1 - alpha),
                       c.b * alpha + base.b * (1 - alpha), 1)
    }
    function glassAlpha(t) {
        if (!t) return 1
        const roles = [[t.foreground, 7, 0.9], [t.brightForeground, 7, 0.9],
                       [t.mutedText, 4.5, 0.85], [t.accent, 3, 0.85]]
        const bases = [over(t.darkerBackground, glassBase, Qt.rgba(0, 0, 0, 1)),
                       over(t.darkerBackground, glassBase, Qt.rgba(1, 1, 1, 1))]
        for (let a = 0.55; a < 1.0; a += 0.01) {
            let ok = true
            for (const [text, floor, share] of roles) {
                const target = Math.min(floor, share * contrast(text, t.background))
                for (const base of bases)
                    if (contrast(text, over(t.background, a, base)) < target) ok = false
            }
            if (ok) return a
        }
        return 1
    }
    property real panelAlpha: glassAlpha(theme)
    Behavior on panelAlpha { NumberAnimation { duration: 450; easing.type: Easing.InOutQuad } }
    Component.onCompleted: console.log("glass", theme.name, glassAlpha(theme).toFixed(2))

    readonly property var actions: {
        const g = guide.game || {}
        const list = [
            { id: "resume", label: "Resume", icon: "play", hint: "" },
            { id: "desktop", label: "Return to desktop", icon: "desktop", hint: "" },
            { id: "replay", label: "Save last 30 seconds", icon: "replay", hint: "Instant replay", live: true },
            { id: "screenshot", label: "Take screenshot", icon: "camera", hint: "" }
        ]
        if (g.steam) list.push({ id: "steam", label: "Steam overlay", icon: "layers", hint: "Shift + Tab" })
        if (g.saveBackup) list.push({ id: "backup", label: "Back up save", icon: "save", hint: g.lastBackup || "" })
        list.push({ id: "library", label: "Open library", icon: "library", hint: "" })
        list.push({ id: "quit", label: "Quit game", icon: "power", hint: "", danger: true })
        return list
    }
    property int current: 0
    readonly property int rowCount: actions.length + 1 // the volume row follows the actions

    function move(step) { current = Math.max(0, Math.min(rowCount - 1, current + step)) }
    function activate() {
        if (current >= actions.length) return
        const a = actions[current]
        if (a.id === "replay") toast.show("Clip saved", (guide.game.title || "Game") + " · last 30 seconds")
        else if (a.id === "screenshot") toast.show("Screenshot saved", "~/Pictures/" + (guide.game.slug || "game") + "-2147.png")
        else if (a.id === "backup") toast.show("Save backed up", guide.game.title || "")
        guide.actionTriggered(a.id)
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Down) { move(1); event.accepted = true }
        else if (event.key === Qt.Key_Up) { move(-1); event.accepted = true }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { activate(); event.accepted = true }
        else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) { guide.closeRequested(); event.accepted = true }
        else if (current === actions.length && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
            guide.volume = Math.max(0, Math.min(1, guide.volume + (event.key === Qt.Key_Right ? 0.05 : -0.05)))
            event.accepted = true
        }
    }

    // ---- Backdrop: the game, frosted and tinted toward the theme --------------------------
    Item {
        id: backdropLayer
        anchors.fill: parent
        opacity: guide.shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        Image {
            id: still
            anchors.fill: parent
            source: guide.backdrop
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        MultiEffect {
            anchors.fill: parent
            source: still
            blurEnabled: true
            blur: 0.55
            blurMax: 48
            saturation: -0.15
            brightness: -0.04
        }
        Rectangle { anchors.fill: parent; color: guide.tint(pal.darkerBg, 0.30) }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: guide.tint(pal.darkerBg, 0.78) }
                GradientStop { position: 0.45; color: guide.tint(pal.darkerBg, 0.35) }
                GradientStop { position: 1.0; color: guide.tint(pal.darkerBg, 0.0) }
            }
        }
    }

    // ---- Clock ------------------------------------------------------------------------
    Item {
        id: clockChip
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 40 * guide.s
        width: clockColumn.width + 56 * guide.s
        height: clockColumn.height + 36 * guide.s
        opacity: guide.shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

        RectangularShadow {
            anchors.fill: parent
            radius: guide.radius
            blur: 40 * guide.s
            offset.y: 12 * guide.s
            color: Qt.rgba(0, 0, 0, 0.35)
        }
        Rectangle { id: clockMask; anchors.fill: parent; radius: guide.radius; visible: false; layer.enabled: true }
        Item {
            anchors.fill: parent
            clip: true
            layer.enabled: guide.radius > 0
            layer.effect: MultiEffect { maskEnabled: true; maskSource: clockMask; maskThresholdMin: 0.5; maskSpreadAtMin: 0.5 }
            Image {
                id: clockUnder
                source: guide.backdrop
                x: -clockChip.x; y: -clockChip.y
                width: guide.width; height: guide.height
                fillMode: Image.PreserveAspectCrop
                visible: false
            }
            MultiEffect {
                source: clockUnder
                x: clockUnder.x; y: clockUnder.y; width: clockUnder.width; height: clockUnder.height
                blurEnabled: true; blur: 1.0; blurMax: 64; saturation: 0.25
            }
            Rectangle { anchors.fill: parent; color: guide.tint(pal.darkerBg, guide.glassBase) }
            Rectangle { anchors.fill: parent; color: guide.tint(pal.bg, guide.panelAlpha) }
        }
        Rectangle {
            anchors.fill: parent
            radius: guide.radius
            color: "transparent"
            border.width: Math.max(1, guide.s)
            border.color: guide.tint(pal.brightFg, guide.theme.mode === "light" ? 0.22 : 0.12)
        }
        Column {
            id: clockColumn
            anchors.centerIn: parent
            spacing: 2 * guide.s
            Text {
                anchors.right: parent.right
                text: guide.clock
                color: pal.brightFg
                font.family: guide.theme.fontFamily
                font.pixelSize: 56 * guide.s
                font.weight: Font.Light
            }
            Text {
                anchors.right: parent.right
                text: guide.date
                color: pal.fg
                font.family: guide.theme.fontFamily
                font.pixelSize: 16 * guide.s
            }
        }
    }

    // ---- Panel ------------------------------------------------------------------------
    Item {
        id: panelShell
        width: 600 * guide.s
        height: parent.height - 2 * y
        x: guide.shown ? 40 * guide.s : 0
        y: 40 * guide.s
        opacity: guide.shown ? 1 : 0
        Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        RectangularShadow {
            anchors.fill: parent
            radius: guide.radius
            blur: 64 * guide.s
            offset.y: 20 * guide.s
            color: Qt.rgba(0, 0, 0, 0.45)
        }
        Rectangle { id: panelMask; anchors.fill: parent; radius: guide.radius; visible: false; layer.enabled: true }

    Item {
        id: panel
        anchors.fill: parent
        clip: true
        layer.enabled: guide.radius > 0
        layer.effect: MultiEffect { maskEnabled: true; maskSource: panelMask; maskThresholdMin: 0.5; maskSpreadAtMin: 0.5 }

        // Glass: the frame behind the panel, heavily blurred, with the game's art washed in at
        // the top, all under one theme tint that keeps the text legible.
        Image {
            id: panelUnder
            source: guide.backdrop
            x: -panelShell.x; y: -panelShell.y
            width: guide.width; height: guide.height
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        MultiEffect {
            source: panelUnder
            x: panelUnder.x; y: panelUnder.y; width: panelUnder.width; height: panelUnder.height
            blurEnabled: true; blur: 1.0; blurMax: 64; saturation: 0.3
        }
        Item {
            id: hero
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 360 * guide.s
            clip: true
            Image {
                id: heroArt
                anchors.fill: parent
                source: guide.game.art || guide.backdrop
                fillMode: Image.PreserveAspectCrop
                visible: false
            }
            Rectangle {
                id: heroFade
                anchors.fill: parent
                visible: false
                layer.enabled: true
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "white" }
                    GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.6) }
                    GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0) }
                }
            }
            MultiEffect {
                anchors.fill: parent
                source: heroArt
                blurEnabled: true; blur: 0.7; blurMax: 40
                saturation: 0.25
                maskEnabled: true
                maskSource: heroFade
                maskSpreadAtMin: 1.0
            }
        }
        Rectangle { anchors.fill: parent; color: guide.tint(pal.darkerBg, guide.glassBase) }
        Rectangle { anchors.fill: parent; color: guide.tint(pal.bg, guide.panelAlpha) }
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 3 * guide.s
            color: pal.accent
        }

        Item {
            id: content
            anchors.fill: parent
            anchors.margins: 36 * guide.s
            anchors.topMargin: 40 * guide.s

            Text {
                id: brand
                text: "OMAKADE"
                color: pal.mutedText
                font.family: guide.theme.fontFamily
                font.pixelSize: 13 * guide.s
                font.letterSpacing: 4 * guide.s
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: brand.verticalCenter
                text: guide.theme.name
                color: pal.mutedText
                font.family: guide.theme.fontFamily
                font.pixelSize: 13 * guide.s
            }

            // Game card
            Item {
                id: header
                anchors { left: parent.left; right: parent.right; top: brand.bottom; topMargin: 24 * guide.s }
                height: 150 * guide.s

                Item {
                    id: coverBox
                    width: 112 * guide.s
                    height: 150 * guide.s
                    Image {
                        id: cover
                        anchors.fill: parent
                        source: guide.game.cover || ""
                        fillMode: Image.PreserveAspectCrop
                        visible: false
                    }
                    Rectangle {
                        id: coverMask
                        anchors.fill: parent
                        radius: guide.radius * 0.6
                        visible: false
                        layer.enabled: true
                    }
                    MultiEffect {
                        anchors.fill: parent
                        source: cover
                        maskEnabled: true
                        maskSource: coverMask
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: guide.radius * 0.6
                        color: "transparent"
                        border.width: Math.max(1, guide.s)
                        border.color: guide.tint(pal.brightFg, 0.12)
                    }
                }

                Column {
                    anchors { left: coverBox.right; leftMargin: 24 * guide.s; right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 8 * guide.s

                    Rectangle {
                        width: sourceText.implicitWidth + 16 * guide.s
                        height: sourceText.implicitHeight + 8 * guide.s
                        radius: guide.radius * 0.4
                        color: guide.tint(pal.accent, 0.14)
                        border.width: Math.max(1, guide.s)
                        border.color: guide.tint(pal.accent, 0.55)
                        Text {
                            id: sourceText
                            anchors.centerIn: parent
                            text: (guide.game.source || "").toUpperCase()
                            color: pal.accent
                            font.family: guide.theme.fontFamily
                            font.pixelSize: 11 * guide.s
                            font.letterSpacing: 2 * guide.s
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        width: parent.width
                        text: guide.game.title || ""
                        color: pal.brightFg
                        font.family: guide.theme.fontFamily
                        font.pixelSize: 32 * guide.s
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "Playing for " + (guide.game.session || "")
                        color: pal.fg
                        font.family: guide.theme.fontFamily
                        font.pixelSize: 16 * guide.s
                    }
                    Text {
                        text: (guide.game.total || "") + " total"
                        color: pal.mutedText
                        font.family: guide.theme.fontFamily
                        font.pixelSize: 14 * guide.s
                    }
                }
            }

            // Achievements
            Item {
                id: achievements
                visible: !!guide.game.achievementsTotal
                anchors { left: parent.left; right: parent.right; top: header.bottom; topMargin: 24 * guide.s }
                height: visible ? 64 * guide.s : 0

                GuideIcon {
                    id: trophy
                    name: "trophy"
                    color: pal.yellow
                    width: 22 * guide.s; height: width
                    stroke: 1.8
                }
                Text {
                    anchors { left: trophy.right; leftMargin: 12 * guide.s; verticalCenter: trophy.verticalCenter }
                    text: (guide.game.achievementsUnlocked || 0) + " of " + (guide.game.achievementsTotal || 0) + " achievements"
                    color: pal.fg
                    font.family: guide.theme.fontFamily
                    font.pixelSize: 15 * guide.s
                }
                Text {
                    anchors { right: parent.right; verticalCenter: trophy.verticalCenter }
                    text: Math.round(100 * (guide.game.achievementsUnlocked || 0) / Math.max(1, guide.game.achievementsTotal || 1)) + "%"
                    color: pal.mutedText
                    font.family: guide.theme.fontFamily
                    font.pixelSize: 14 * guide.s
                }
                Rectangle {
                    id: track
                    anchors { left: parent.left; right: parent.right; top: trophy.bottom; topMargin: 12 * guide.s }
                    height: 5 * guide.s
                    radius: height / 2
                    color: guide.tint(pal.lighterBg, 0.9)
                    Rectangle {
                        height: parent.height
                        radius: parent.radius
                        width: guide.shown ? parent.width * (guide.game.achievementsUnlocked || 0) / Math.max(1, guide.game.achievementsTotal || 1) : 0
                        Behavior on width { NumberAnimation { duration: 700; easing.type: Easing.OutCubic } }
                        color: pal.accent
                    }
                }
                Text {
                    anchors { left: parent.left; right: parent.right; top: track.bottom; topMargin: 10 * guide.s }
                    text: guide.game.latestAchievement ? "Latest: " + guide.game.latestAchievement : ""
                    color: pal.mutedText
                    font.family: guide.theme.fontFamily
                    font.pixelSize: 13 * guide.s
                    elide: Text.ElideRight
                }
            }

            // Actions
            Item {
                id: list
                anchors { left: parent.left; right: parent.right; top: achievements.bottom; topMargin: 26 * guide.s }
                height: guide.actions.length * rowHeight
                readonly property real rowHeight: 52 * guide.s

                Rectangle {
                    id: highlight
                    visible: guide.current < guide.actions.length
                    width: parent.width + 24 * guide.s
                    x: -12 * guide.s
                    height: list.rowHeight
                    y: Math.min(guide.current, guide.actions.length - 1) * list.rowHeight
                    Behavior on y { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                    radius: guide.radius * 0.6
                    color: guide.tint(pal.selection, 0.85)
                    Rectangle {
                        width: 3 * guide.s
                        height: parent.height - 18 * guide.s
                        anchors.verticalCenter: parent.verticalCenter
                        x: 0
                        radius: width / 2
                        color: (guide.actions[guide.current] || {}).danger ? pal.red : pal.accent
                    }
                }

                Repeater {
                    model: guide.actions
                    delegate: Item {
                        required property var modelData
                        required property int index
                        readonly property bool focused: guide.current === index
                        width: list.width
                        height: list.rowHeight
                        y: index * list.rowHeight
                        opacity: guide.shown ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

                        GuideIcon {
                            id: rowIcon
                            anchors { left: parent.left; leftMargin: 6 * guide.s; verticalCenter: parent.verticalCenter }
                            width: 22 * guide.s; height: width
                            name: modelData.icon
                            color: modelData.danger ? pal.red : (parent.focused ? pal.brightFg : pal.fg)
                        }
                        Text {
                            anchors { left: rowIcon.right; leftMargin: 18 * guide.s; verticalCenter: parent.verticalCenter }
                            text: modelData.label
                            color: modelData.danger ? pal.red : (parent.focused ? pal.brightFg : pal.fg)
                            font.family: guide.theme.fontFamily
                            font.pixelSize: 18 * guide.s
                            font.weight: parent.focused ? Font.DemiBold : Font.Normal
                        }
                        Row {
                            anchors { right: parent.right; rightMargin: 4 * guide.s; verticalCenter: parent.verticalCenter }
                            spacing: 8 * guide.s
                            Rectangle {
                                visible: !!modelData.live
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8 * guide.s; height: width; radius: width / 2
                                color: pal.red
                                SequentialAnimation on opacity {
                                    loops: Animation.Infinite
                                    running: !!modelData.live && guide.shown
                                    NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                                    NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.hint
                                color: pal.mutedText
                                font.family: guide.theme.fontFamily
                                font.pixelSize: 13 * guide.s
                            }
                        }
                    }
                }
            }

            // Status: controllers and sound
            Column {
                id: status
                anchors { left: parent.left; right: parent.right; bottom: footer.top; bottomMargin: 24 * guide.s }
                spacing: 14 * guide.s

                Rectangle { width: parent.width; height: Math.max(1, guide.s); color: guide.tint(pal.lighterBg, 0.9) }

                Row {
                    width: status.width
                    height: 24 * guide.s
                    spacing: 28 * guide.s
                    Repeater {
                        model: guide.game.controllers || []
                        delegate: Row {
                            required property var modelData
                            spacing: 10 * guide.s
                            height: 24 * guide.s
                            GuideIcon {
                                name: "gamepad"
                                color: pal.fg
                                width: 22 * guide.s; height: width
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name
                                color: pal.fg
                                font.family: guide.theme.fontFamily
                                font.pixelSize: 14 * guide.s
                            }
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2 * guide.s
                                Repeater {
                                    model: 4
                                    Rectangle {
                                        required property int index
                                        width: 6 * guide.s; height: 12 * guide.s
                                        radius: 1.5 * guide.s
                                        readonly property real level: modelData.battery
                                        color: index < Math.ceil(level * 4)
                                               ? (level <= 0.25 ? pal.red : level <= 0.5 ? pal.yellow : pal.green)
                                               : guide.tint(pal.lighterBg, 0.9)
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    id: volumeRow
                    readonly property bool focused: guide.current === guide.actions.length
                    width: status.width
                    height: 48 * guide.s
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: -12 * guide.s
                        anchors.rightMargin: -12 * guide.s
                        radius: guide.radius * 0.6
                        color: guide.tint(pal.selection, 0.85)
                        opacity: volumeRow.focused ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 150 } }
                    }
                    GuideIcon {
                        id: volIcon
                        name: "volume"
                        color: volumeRow.focused ? pal.brightFg : pal.fg
                        width: 22 * guide.s; height: width
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: outputLabel
                        anchors { left: volIcon.right; leftMargin: 14 * guide.s; verticalCenter: parent.verticalCenter }
                        text: guide.game.output || "Speakers"
                        color: volumeRow.focused ? pal.brightFg : pal.fg
                        font.family: guide.theme.fontFamily
                        font.pixelSize: 15 * guide.s
                        width: 150 * guide.s
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: volTrack
                        anchors { left: outputLabel.right; leftMargin: 16 * guide.s; right: volValue.left; rightMargin: 16 * guide.s; verticalCenter: parent.verticalCenter }
                        height: 5 * guide.s
                        radius: height / 2
                        color: guide.tint(pal.lighterBg, 0.9)
                        Rectangle {
                            width: parent.width * guide.volume
                            height: parent.height
                            radius: parent.radius
                            color: pal.accent
                            Behavior on width { NumberAnimation { duration: 120 } }
                        }
                        Rectangle {
                            x: parent.width * guide.volume - width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: (volumeRow.focused ? 16 : 12) * guide.s
                            height: width
                            radius: width / 2
                            color: pal.brightFg
                            Behavior on x { NumberAnimation { duration: 120 } }
                            Behavior on width { NumberAnimation { duration: 150 } }
                        }
                    }
                    Text {
                        id: volValue
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: Math.round(guide.volume * 100) + "%"
                        color: pal.mutedText
                        font.family: guide.theme.fontFamily
                        font.pixelSize: 14 * guide.s
                        width: 44 * guide.s
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            // Button hints
            Row {
                id: footer
                anchors { left: parent.left; bottom: parent.bottom }
                spacing: 28 * guide.s
                Repeater {
                    model: [
                        { glyph: "A", label: "Select" },
                        { glyph: "B", label: "Back" },
                        { glyph: "", icon: "home", label: "Close" }
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: 10 * guide.s
                        Rectangle {
                            width: 26 * guide.s; height: width; radius: width / 2
                            color: "transparent"
                            border.width: Math.max(1, 1.5 * guide.s)
                            border.color: pal.fg
                            Text {
                                visible: modelData.glyph !== ""
                                anchors.centerIn: parent
                                text: modelData.glyph
                                color: pal.fg
                                font.family: guide.theme.fontFamily
                                font.pixelSize: 13 * guide.s
                                font.weight: Font.DemiBold
                            }
                            GuideIcon {
                                visible: !!modelData.icon
                                anchors.centerIn: parent
                                width: 15 * guide.s; height: width
                                name: modelData.icon || ""
                                color: pal.fg
                                stroke: 2.2
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: pal.mutedText
                            font.family: guide.theme.fontFamily
                            font.pixelSize: 14 * guide.s
                        }
                    }
                }
            }
        }
    }
        Rectangle {
            anchors.fill: parent
            radius: guide.radius
            color: "transparent"
            border.width: Math.max(1, guide.s)
            border.color: guide.tint(pal.brightFg, guide.theme.mode === "light" ? 0.22 : 0.12)
        }
    }

    // ---- Toast --------------------------------------------------------------------------
    Rectangle {
        id: toast
        property string title: ""
        property string detail: ""
        function show(t, d) { title = t; detail = d; toastTimer.restart(); visibleState = true }
        property bool visibleState: false
        anchors.right: parent.right
        anchors.rightMargin: 64 * guide.s
        y: parent.height - height - (visibleState ? 64 : 32) * guide.s
        opacity: visibleState ? 1 : 0
        Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 220 } }
        width: 420 * guide.s
        height: 76 * guide.s
        radius: guide.radius
        color: guide.tint(pal.bg, 0.95)
        border.width: Math.max(1, guide.s)
        border.color: guide.tint(pal.accent, 0.6)
        Timer { id: toastTimer; interval: 2600; onTriggered: toast.visibleState = false }
        Rectangle {
            id: toastBadge
            anchors { left: parent.left; leftMargin: 18 * guide.s; verticalCenter: parent.verticalCenter }
            width: 36 * guide.s; height: width; radius: width / 2
            color: guide.tint(pal.green, 0.18)
            GuideIcon { anchors.centerIn: parent; width: 20 * guide.s; height: width; name: "check"; color: pal.green; stroke: 2.2 }
        }
        Column {
            anchors { left: toastBadge.right; leftMargin: 16 * guide.s; right: parent.right; rightMargin: 16 * guide.s; verticalCenter: parent.verticalCenter }
            spacing: 4 * guide.s
            Text { text: toast.title; color: pal.brightFg; font.family: guide.theme.fontFamily; font.pixelSize: 16 * guide.s; font.weight: Font.DemiBold }
            Text { width: parent.width; text: toast.detail; color: pal.mutedText; font.family: guide.theme.fontFamily; font.pixelSize: 13 * guide.s; elide: Text.ElideRight }
        }
    }
}
