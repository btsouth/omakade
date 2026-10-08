pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import "Contrast.js" as Contrast

// Standalone design prototype. All actions below change fixture state only.
FocusScope {
    id: guide
    property var theme
    property var game: ({})
    property url backdrop
    property bool shown: false
    property int current: 0
    property string page: "home"
    property real volume: 0.72
    property int performance: 1
    property int frameLimit: 1
    property int output: 0
    property int display: 0
    property bool micMuted: true
    property bool playing: true
    property bool confirmingQuit: false
    property string notice: ""
    property string clock: "21:47"
    property string date: "Sunday, October 4"
    signal actionTriggered(string action)
    signal closeRequested()
    readonly property real s: height / 1080
    readonly property real radius: Math.max(0, theme ? theme.cornerRadius : 0) * s
    // Theme colours update together; Preview cross-fades the complete old render.
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
    }
    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }


    readonly property color dimColor: theme.mode === "light" ? pal.fg : pal.darkerBg
    readonly property real panelAlpha: Contrast.alpha(theme.t)
    readonly property var levels: ["Off", "FPS", "FPS + frame time", "Full"]
    readonly property var limits: ["Off", "60 fps", "90 fps", "120 fps"]
    readonly property var displays: ["Living room TV", "Desk monitor"]
    readonly property var outputs: [game.output || "Living room TV", "Headphones", "Speakers"]
    readonly property var actions: {
        if (confirmingQuit) return [
            {id: "cancel", label: "Keep playing", icon: "play", detail: ""},
            {id: "confirm", label: "Quit " + game.title, icon: "power", detail: "Confirm"}];
        if (page === "tools") return [
            {id: "performance", label: "Performance", icon: "stats", detail: levels[performance]},
            {id: "limit", label: "Frame limit", icon: "stats", detail: limits[frameLimit]},
            {id: "output", label: "Sound output", icon: "volume", detail: outputs[output]},
            {id: "display", label: "Display", icon: "desktop", detail: displays[display]},
            {id: "volume", label: "Output volume", icon: "volume", detail: Math.round(volume * 100) + "%"},
            {id: "mic", label: "Microphone", icon: "mic", detail: micMuted ? "Muted" : "On"},
            {id: "music", label: playing ? "Pause music" : "Play music", icon: playing ? "pause" : "play", detail: ""},
            {id: "skip", label: "Next track", icon: "next", detail: ""},
            {id: "back", label: "Back to guide", icon: "back", detail: ""}];
        const list = [
            {id: "resume", label: "Resume", icon: "play", detail: ""},
            {id: "replay", label: "Save last 30 seconds", icon: "replay", detail: ""},
            {id: "screenshot", label: "Take screenshot", icon: "camera", detail: ""},
            {id: "tools", label: "Controls & more", icon: "sliders", detail: "→"}];
        if (game.steam) list.push({id: "steam", label: "Steam overlay", icon: "layers", detail: "Shift + Tab"});
        if (game.saveBackup) list.push({id: "backup", label: "Back up save", icon: "save", detail: game.lastBackup});
        list.push({id: "library", label: "Open library", icon: "library", detail: ""});
        list.push({id: "desktop", label: "Return to desktop", icon: "desktop", detail: ""});
        list.push({id: "quit", label: "Quit game…", icon: "power", detail: ""});
        return list;
    }
    readonly property var navigation: {
        if (confirmingQuit) return [0, 1];
        if (page === "tools") return [0, 1, 2, 3, 4, 5, 6, 7, 8];
        return [0, 1, 2, actions.length, actions.length + 1, actions.length + 2].concat(actions.slice(3).map((a, i) => i + 3));
    }
    function order(index) { return navigation.indexOf(index) + 1 }
    function move(step) {
        const position = Math.max(0, navigation.indexOf(current));
        current = navigation[(position + step + navigation.length) % navigation.length];
    }
    function back() {
        if (confirmingQuit) { confirmingQuit = false; current = actions.length - 1; return }
        if (page !== "home") { page = "home"; current = 3; return }
        closeRequested()
    }
    function adjust(step) {
        if (page === "home" && !confirmingQuit) {
            if (current === 1 || current === 2) current = step > 0 ? 2 : 1;
            else if (current >= actions.length) current = actions.length + (current - actions.length + step + 3) % 3;
            return;
        }
        if (page === "tools" && (current === 6 || current === 7)) { current = step > 0 ? 7 : 6; return }
        const id = (actions[current] || {}).id;
        if (id === "performance") performance = (performance + step + levels.length) % levels.length;
        else if (id === "limit") frameLimit = (frameLimit + step + limits.length) % limits.length;
        else if (id === "output") output = (output + step + outputs.length) % outputs.length;
        else if (id === "display") display = (display + step + displays.length) % displays.length;
        else if (id === "volume") volume = Math.max(0, Math.min(1, volume + step * 0.05));
        else if (id === "mic") micMuted = !micMuted;
        else if (id === "music") playing = !playing;
    }
    function notify(message) { notice = message; noticeTimer.restart() }
    function activate() {
        const id = (actions[current] || {}).id;
        if (!id) { notify(current === actions.length + 1 ? "30-second clip selected" : "Screenshot selected"); return }
        if (id === "resume") { closeRequested(); return }
        if (id === "tools") { page = "tools"; current = 0 }
        else if (id === "back" || id === "cancel") back();
        else if (id === "quit") { confirmingQuit = true; current = 0 }
        else if (id === "confirm") { notify("Quit confirmed · preview only"); back() }
        else if (id === "replay") notify("Clip saved · last 30 seconds");
        else if (id === "screenshot") notify("Screenshot saved");
        else if (id === "backup") notify("Save backed up");
        else if (id === "skip") notify("Next track · preview only");
        else if (id === "music") playing = !playing;
        else if (["performance", "limit", "output", "display", "volume", "mic"].indexOf(id) >= 0) adjust(1);
        else notify(actions[current].label + " · preview only");
        actionTriggered(id)
    }
    Keys.onPressed: event => {
        if (!shown) return;
        if (event.key === Qt.Key_Down) move(1);
        else if (event.key === Qt.Key_Up) move(-1);
        else if (event.key === Qt.Key_Left) adjust(-1);
        else if (event.key === Qt.Key_Right) adjust(1);
        else if ([Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space].indexOf(event.key) >= 0) activate();
        else if ([Qt.Key_Escape, Qt.Key_Backspace].indexOf(event.key) >= 0) back();
        else return;
        event.accepted = true;
        console.log("FOCUS", page, current + 1, (actions[current] || {}).id || "capture", performance, frameLimit, output, volume, micMuted)
    }

    // A gentle dim retains the scene; stronger, saturated frost stays inside the glass.
    Item {
        anchors.fill: parent; opacity: guide.shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 240 } }
        Rectangle { anchors.fill: parent; color: guide.tint(guide.dimColor, 0.22) }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: guide.tint(guide.dimColor, 0.3) }
                GradientStop { position: 0.55; color: guide.tint(guide.dimColor, 0) }
                GradientStop { position: 1; color: guide.tint(guide.dimColor, 0.05) }
            }
        }
    }
    GlassSurface {
        id: panel
        x: guide.shown ? 40 * guide.s : -width
        y: 40 * guide.s; width: 700 * guide.s; height: guide.height - 80 * guide.s
        opacity: guide.shown ? 1 : 0
        Behavior on x { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 220 } }
        colors: pal; backdrop: guide.backdrop; alpha: guide.panelAlpha
        radius: guide.radius; scaleFactor: guide.s
        frameWidth: guide.width; frameHeight: guide.height
        // One continuous protected interior; hierarchy comes from artwork,
        // typography, action size and spacing rather than per-row material seams.
        rim: 6 * guide.s
        component Label: Text {
            color: pal.fg; font.family: guide.theme.fontFamily; font.pixelSize: 16 * guide.s
        }
        component Surface: GlassSurface {
            framed: false; shadowed: false
            colors: pal; backdrop: guide.backdrop; alpha: guide.panelAlpha
            radius: guide.radius; scaleFactor: guide.s
            frameWidth: guide.width; frameHeight: guide.height
            originX: panel.x + x; originY: panel.y + y
        }
        component Tile: GuideTile {
            colors: pal; backdrop: guide.backdrop; alpha: guide.panelAlpha
            radius: guide.radius; scaleFactor: guide.s; family: guide.theme.fontFamily
            frameWidth: guide.width; frameHeight: guide.height
            originX: panel.x + x; originY: panel.y + y
        }
        Label {
            x: 30 * guide.s; y: 29 * guide.s
            text: "OMAKADE"; color: pal.mutedText
            font.pixelSize: 12 * guide.s; font.letterSpacing: 3 * guide.s
        }
        Label {
            x: 520 * guide.s; y: 29 * guide.s; width: 150 * guide.s
            text: "IN-GAME GUIDE"; color: pal.mutedText
            font.pixelSize: 11 * guide.s; horizontalAlignment: Text.AlignRight
        }
        // Artwork has its own space. Text begins below it on audited theme glass;
        // the cover overlaps that transition to give the game card real depth.
        Surface {
            id: gameCard
            x: 30 * guide.s; y: 64 * guide.s; width: 640 * guide.s; height: 318 * guide.s
            zones: [{x: 0, y: 150*guide.s, width: width, height: height - 150*guide.s}]
            Image {
                width: parent.width; height: 150 * guide.s
                source: guide.game.art || guide.backdrop; fillMode: Image.PreserveAspectCrop
            }
            RectangularShadow {
                x: 18 * guide.s; y: 58 * guide.s; width: 118 * guide.s; height: 156 * guide.s
                radius: guide.radius * 0.5; blur: 14 * guide.s; offset.y: 4 * guide.s
                color: Qt.rgba(0, 0, 0, 0.32)
            }
            Image {
                x: 18 * guide.s; y: 58 * guide.s; width: 118 * guide.s; height: 156 * guide.s
                source: guide.game.cover || ""; fillMode: Image.PreserveAspectCrop
            }
            Label { x: 158 * guide.s; y: 160 * guide.s; text: (guide.game.source || "").toUpperCase(); color: pal.accent; font.pixelSize: 11 * guide.s; font.letterSpacing: 1.8 * guide.s }
            Label { x: 158 * guide.s; y: 181 * guide.s; width: 462 * guide.s; text: guide.game.title || ""; color: pal.brightFg; font.pixelSize: 32 * guide.s; font.weight: Font.DemiBold; elide: Text.ElideRight }
            Label { x: 158 * guide.s; y: 223 * guide.s; text: guide.game.session + " this session  ·  " + guide.game.total + " total"; color: pal.mutedText; font.pixelSize: 12 * guide.s }
            GuideIcon { x: 20 * guide.s; y: 253 * guide.s; width: 18 * guide.s; height: width; name: "trophy"; color: pal.accent }
            Label { x: 50 * guide.s; y: 253 * guide.s; text: guide.game.achievementsUnlocked + " / " + guide.game.achievementsTotal + " achievements"; font.pixelSize: 13 * guide.s }
            Label { x: 570 * guide.s; y: 253 * guide.s; text: Math.round(100 * guide.game.achievementsUnlocked / guide.game.achievementsTotal) + "%"; color: pal.mutedText; font.pixelSize: 13 * guide.s }
            Rectangle {
                x: 20 * guide.s; y: 279 * guide.s; width: 600 * guide.s; height: 3 * guide.s
                color: guide.tint(pal.fg, 0.14)
                Rectangle { width: parent.width * guide.game.achievementsUnlocked / guide.game.achievementsTotal; height: parent.height; color: pal.accent }
            }
            Label { x: 20 * guide.s; y: 292 * guide.s; width: 600 * guide.s; text: guide.game.latestAchievement || ""; color: pal.mutedText; font.pixelSize: 11 * guide.s; elide: Text.ElideRight }
        }
        Item {
            anchors.fill: parent; visible: guide.page === "home" && !guide.confirmingQuit
            Tile {
                x: 30 * guide.s; y: 394 * guide.s; width: 640 * guide.s; height: 68 * guide.s
                primary: true; label: "Resume"; detail: "Return to " + guide.game.title; icon: "play"
                order: guide.order(0); selected: guide.current === 0
                onClicked: { guide.current = 0; guide.activate() }
            }
            Tile {
                x: 30 * guide.s; y: 474 * guide.s; width: 314 * guide.s; height: 88 * guide.s
                label: "Save replay"; detail: "Last 30 seconds"; icon: "replay"
                order: guide.order(1); selected: guide.current === 1
                onClicked: { guide.current = 1; guide.activate() }
            }
            Tile {
                x: 356 * guide.s; y: 474 * guide.s; width: 314 * guide.s; height: 88 * guide.s
                label: "Screenshot"; detail: "Keep this moment"; icon: "camera"
                order: guide.order(2); selected: guide.current === 2
                onClicked: { guide.current = 2; guide.activate() }
            }
            Surface {
                x: 30 * guide.s; y: 568 * guide.s; width: 640 * guide.s; height: 76 * guide.s
                Repeater {
                    model: ["Screenshot · 2m", "Clip · 30s", "Screenshot · 18m"]
                    delegate: Item {
                        id: capture
                        required property string modelData
                        required property int index
                        x: (14 + index * 210) * guide.s; y: 8 * guide.s
                        width: 196 * guide.s; height: 62 * guide.s
                        Image { width: parent.width; height: 40 * guide.s; source: capture.index === 1 ? "game2.jpg" : guide.backdrop; fillMode: Image.PreserveAspectCrop; clip: true }
                        Rectangle { width: parent.width; height: 40 * guide.s; color: "transparent"; border.width: guide.current === guide.actions.length + capture.index ? 2 : 0; border.color: pal.accent }
                        Label { y: 46 * guide.s; text: guide.order(guide.actions.length + capture.index) + "  " + capture.modelData; font.pixelSize: 11 * guide.s; color: pal.mutedText }
                        MouseArea { anchors.fill: parent; onClicked: { guide.current = guide.actions.length + capture.index; guide.activate() } }
                    }
                }
            }
            Surface {
                x: 30 * guide.s; y: 656 * guide.s; width: 640 * guide.s; height: 180 * guide.s
                Repeater {
                    model: guide.actions.slice(3)
                    delegate: GuideRow {
                        required property var modelData
                        required property int index
                        x: 20 * guide.s; y: index * 36 * guide.s
                        width: 600 * guide.s; height: 36 * guide.s; compact: true; s: guide.s
                        colors: pal; family: guide.theme.fontFamily
                        label: modelData.label; detail: modelData.detail; icon: modelData.icon
                        order: guide.order(index + 3); selected: guide.current === index + 3
                        onClicked: { guide.current = index + 3; guide.activate() }
                    }
                }
            }
        }
        Item {
            anchors.fill: parent; visible: guide.page === "tools" && !guide.confirmingQuit
            Surface {
                x: 30 * guide.s; y: 394 * guide.s; width: 640 * guide.s; height: 40 * guide.s
                Label { x: 20 * guide.s; anchors.verticalCenter: parent.verticalCenter; text: "Controls & more"; font.pixelSize: 21 * guide.s; font.weight: Font.Medium }
            }
            Tile {
                x: 30 * guide.s; y: 446 * guide.s; width: 314 * guide.s; height: 104 * guide.s
                setting: true; label: "Performance"; detail: guide.levels[guide.performance]; icon: "stats"
                order: guide.order(0); selected: guide.current === 0
                onClicked: { guide.current = 0; guide.activate() }
            }
            Tile {
                x: 356 * guide.s; y: 446 * guide.s; width: 314 * guide.s; height: 104 * guide.s
                setting: true; label: "Frame limit"; detail: guide.limits[guide.frameLimit]; icon: "stats"
                order: guide.order(1); selected: guide.current === 1
                onClicked: { guide.current = 1; guide.activate() }
            }
            Surface {
                x: 30 * guide.s; y: 562 * guide.s; width: 640 * guide.s; height: 172 * guide.s
                Label { x: 24 * guide.s; y: 14 * guide.s; text: "SOUND & DISPLAY"; font.pixelSize: 11 * guide.s; font.letterSpacing: 1.5 * guide.s; color: pal.mutedText }
                Repeater {
                    model: guide.actions.slice(2, 6)
                    delegate: GuideRow {
                        required property var modelData
                        required property int index
                        x: 20 * guide.s; y: (34 + index * 32) * guide.s
                        width: 600 * guide.s; height: 32 * guide.s; compact: true; s: guide.s
                        colors: pal; family: guide.theme.fontFamily
                        label: modelData.label; detail: modelData.detail; icon: modelData.icon
                        order: guide.order(index + 2); selected: guide.current === index + 2
                        onClicked: { guide.current = index + 2; guide.activate() }
                    }
                }
            }
            Surface {
                x: 30 * guide.s; y: 746 * guide.s; width: 404 * guide.s; height: 108 * guide.s
                Label { x: 20 * guide.s; y: 14 * guide.s; text: "NOW PLAYING"; color: pal.mutedText; font.pixelSize: 10 * guide.s; font.letterSpacing: 1.5 * guide.s }
                Label { x: 20 * guide.s; y: 35 * guide.s; text: "Night Drive"; color: pal.brightFg; font.pixelSize: 20 * guide.s }
                Label { x: 20 * guide.s; y: 64 * guide.s; text: "Chromatic Coast · " + (guide.playing ? "Playing" : "Paused"); color: pal.mutedText; font.pixelSize: 11 * guide.s }
                Repeater {
                    model: [6, 7]
                    delegate: Item {
                        required property int modelData
                        x: (modelData === 6 ? 276 : 340) * guide.s; y: 29 * guide.s; width: 44 * guide.s; height: 64 * guide.s
                        Rectangle { width: parent.width; height: 44 * guide.s; radius: guide.radius; color: "transparent"; border.width: guide.current === parent.modelData ? 2 : 1; border.color: guide.tint(pal.accent, guide.current === parent.modelData ? 1 : 0.25) }
                        GuideIcon { anchors.horizontalCenter: parent.horizontalCenter; y: 11 * guide.s; width: 22 * guide.s; height: width; name: (guide.actions[parent.modelData] || {}).icon || "play"; color: pal.fg }
                        Label { anchors.horizontalCenter: parent.horizontalCenter; y: 50 * guide.s; text: guide.order(parent.modelData); color: pal.mutedText; font.pixelSize: 10 * guide.s }
                        MouseArea { anchors.fill: parent; onClicked: { guide.current = parent.modelData; guide.activate() } }
                    }
                }
            }
            Surface {
                x: 30 * guide.s; y: 864 * guide.s; width: 640 * guide.s; height: 40 * guide.s
                GuideRow {
                    x: 20 * guide.s; width: 600 * guide.s; height: parent.height; s: guide.s; compact: true
                    colors: pal; family: guide.theme.fontFamily; label: "Back to guide"; icon: "back"
                    order: guide.order(8); selected: guide.current === 8
                    onClicked: { guide.current = 8; guide.activate() }
                }
            }
        }
        Surface {
            x: 30 * guide.s; y: 394 * guide.s; width: 640 * guide.s; height: 184 * guide.s
            visible: guide.confirmingQuit
            Label { x: 24 * guide.s; y: 18 * guide.s; text: "Quit " + guide.game.title + "?"; width: 592 * guide.s; elide: Text.ElideRight; font.pixelSize: 24 * guide.s; font.weight: Font.DemiBold }
            Label { x: 24 * guide.s; y: 56 * guide.s; text: "Your game will close. B keeps it running."; color: pal.mutedText; font.pixelSize: 13 * guide.s }
            Repeater {
                model: guide.actions
                delegate: GuideRow {
                    required property var modelData
                    required property int index
                    x: 20 * guide.s; y: (90 + index * 42) * guide.s
                    width: 600 * guide.s; height: 42 * guide.s; compact: true; s: guide.s
                    colors: pal; family: guide.theme.fontFamily; label: modelData.label; icon: modelData.icon
                    order: index + 1; selected: guide.current === index
                    onClicked: { guide.current = index; guide.activate() }
                }
            }
        }
        Surface {
            x: 30 * guide.s; y: (guide.page === "home" ? 850 : 910) * guide.s
            width: 640 * guide.s; height: (guide.page === "home" ? 84 : 40) * guide.s
            Repeater {
                model: guide.game.controllers || []
                delegate: Item {
                    id: controller
                    required property var modelData
                    required property int index
                    x: 20 * guide.s; y: (guide.page === "home" ? 8 + index * 20 : 2 + index * 18) * guide.s; width: 600 * guide.s; height: 20 * guide.s
                    GuideIcon { name: "gamepad"; color: pal.fg; width: 20 * guide.s; height: width }
                    Label { x: 32 * guide.s; text: controller.modelData.name; color: pal.mutedText; font.pixelSize: 12 * guide.s }
                    Row {
                        x: 520 * guide.s; y: 4 * guide.s; spacing: 3 * guide.s
                        Repeater {
                            model: 4
                            delegate: Rectangle {
                                required property int index
                                width: 4 * guide.s; height: 10 * guide.s; radius: guide.radius * 0.1
                                color: index < Math.ceil(controller.modelData.battery * 4) ? pal.accent : guide.tint(pal.fg, 0.14)
                            }
                        }
                    }
                    Label { x: 554 * guide.s; text: Math.round(controller.modelData.battery * 100) + "%"; font.pixelSize: 12 * guide.s }
                }
            }
            Item {
                visible: guide.page === "home"; x: 20 * guide.s; y: 56 * guide.s; width: 600 * guide.s; height: 22 * guide.s
                GuideIcon { name: "volume"; width: 18 * guide.s; height: width; color: pal.fg }
                Label { x: 30 * guide.s; text: guide.outputs[guide.output]; color: pal.mutedText; font.pixelSize: 12 * guide.s }
                Rectangle {
                    x: 310 * guide.s; y: 8 * guide.s; width: 150 * guide.s; height: 3 * guide.s; color: guide.tint(pal.fg, 0.14)
                    Rectangle { width: parent.width * guide.volume; height: parent.height; color: pal.accent }
                }
                Label { x: 474 * guide.s; text: Math.round(guide.volume * 100) + "%"; font.pixelSize: 12 * guide.s }
                GuideIcon { x: 542 * guide.s; name: "mic"; width: 18 * guide.s; height: width; color: pal.fg }
                Label { x: 570 * guide.s; text: guide.micMuted ? "Off" : "On"; color: pal.mutedText; font.pixelSize: 12 * guide.s }
            }
        }
        Row {
            x: 30 * guide.s; y: 956 * guide.s; spacing: 28 * guide.s
            Repeater {
                model: ["↕ Move", "A Select", "B Back", "⌂ Close"]
                delegate: Label { required property string modelData; text: modelData; font.pixelSize: 13 * guide.s }
            }
        }
    }
    GlassSurface {
        x: guide.width - width - 40 * guide.s; y: 40 * guide.s
        width: 246 * guide.s; height: 108 * guide.s
        colors: pal; backdrop: guide.backdrop; alpha: guide.panelAlpha
        radius: guide.radius; scaleFactor: guide.s; frameWidth: guide.width; frameHeight: guide.height
        visible: guide.shown
        Column {
            anchors.centerIn: parent; spacing: 4 * guide.s
            Text { anchors.right: parent.right; text: guide.clock; color: pal.brightFg; font.family: guide.theme.fontFamily; font.pixelSize: 44 * guide.s; font.weight: Font.Light }
            Text { text: guide.date; color: pal.fg; font.family: guide.theme.fontFamily; font.pixelSize: 13 * guide.s }
        }
    }
    GlassSurface {
        x: guide.width - width - 40 * guide.s; y: guide.height - height - 40 * guide.s
        width: 430 * guide.s; height: 82 * guide.s; visible: guide.shown && guide.notice !== ""
        colors: pal; backdrop: guide.backdrop; alpha: guide.panelAlpha
        radius: guide.radius; scaleFactor: guide.s; frameWidth: guide.width; frameHeight: guide.height
        Text { anchors.centerIn: parent; text: guide.notice; color: pal.fg; font.family: guide.theme.fontFamily; font.pixelSize: 16 * guide.s }
    }
    Timer { id: noticeTimer; interval: 2600; onTriggered: guide.notice = "" }
}
