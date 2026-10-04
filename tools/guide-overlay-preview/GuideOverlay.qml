pragma ComponentBehavior: Bound
import QtQuick
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
    property string note: "Take the ferry after sunset."
    property bool editingNote: false
    property string noteDraft: ""
    property int noteCursor: 0
    readonly property var noteKeys: "ABCDEFGHIJKLMNOPQRSTUVWXYZ".split("").concat(["Space", "⌫", "Save", "Cancel"])
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
            {id: "notes", label: "Game notes", icon: "notes", detail: "Edit"},
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
    readonly property int rowCount: actions.length + (page === "home" && !confirmingQuit ? 3 : 0)
    function move(step) { current = (current + step + rowCount) % rowCount }
    function back() {
        if (editingNote) { editingNote = false; forceActiveFocus(); return }
        if (confirmingQuit) { confirmingQuit = false; current = actions.length - 1; return }
        if (page !== "home") { page = "home"; current = 3; return }
        closeRequested()
    }
    function adjust(step) {
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
        else if (id === "notes") { noteDraft = note; noteCursor = 0; editingNote = true }
        else if (id === "skip") notify("Next track · preview only");
        else if (["performance", "limit", "output", "display", "volume", "mic", "music"].indexOf(id) >= 0) adjust(1);
        else notify(actions[current].label + " · preview only");
        actionTriggered(id)
    }
    Keys.onPressed: event => {
        if (!shown) return;
        if (editingNote) {
            if (event.key === Qt.Key_Down) noteCursor = (noteCursor + 6) % noteKeys.length;
            else if (event.key === Qt.Key_Up) noteCursor = (noteCursor + noteKeys.length - 6) % noteKeys.length;
            else if (event.key === Qt.Key_Left) noteCursor = (noteCursor + noteKeys.length - 1) % noteKeys.length;
            else if (event.key === Qt.Key_Right) noteCursor = (noteCursor + 1) % noteKeys.length;
            else if ([Qt.Key_Return, Qt.Key_Enter].indexOf(event.key) >= 0) {
                const key = noteKeys[noteCursor];
                if (key === "Save") { note = noteDraft; editingNote = false; notify("Note saved · preview only") }
                else if (key === "Cancel") editingNote = false;
                else if (key === "⌫") noteDraft = noteDraft.slice(0, -1);
                else if (noteDraft.length < 180) noteDraft += key === "Space" ? " " : key.toLowerCase();
            } else if (event.key === Qt.Key_Escape) editingNote = false;
            else return;
            event.accepted = true; return;
        }
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
        y: 40 * guide.s; width: 620 * guide.s; height: guide.height - 80 * guide.s
        opacity: guide.shown ? 1 : 0
        Behavior on x { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 220 } }
        colors: pal; backdrop: guide.backdrop; alpha: guide.panelAlpha
        radius: guide.radius; scaleFactor: guide.s; rim: 9 * guide.s
        frameWidth: guide.width; frameHeight: guide.height
        // Frost channels separate the header, action rows and device status. Text never
        // enters these channels: every label stays inside a full-strength audited zone.
        zones: {
            const k = guide.s;
            const z = [{x: 24*k, y: 24*k, width: 572*k, height: 310*k},
                       {x: 24*k, y: (guide.game.controllers.length > 1 ? 800 : 820)*k, width: 572*k, height: (guide.game.controllers.length > 1 ? 176 : 156)*k}];
            const rowHeight = guide.page === "tools" ? 36 : 43;
            for (let i = 0; i < guide.actions.length; ++i)
                z.push({x: 24*k, y: (343 + i*rowHeight)*k, width: 572*k, height: (rowHeight - 5)*k});
            const below = 343 + guide.actions.length*rowHeight + 16;
            if (guide.confirmingQuit) z.push({x: 24*k, y: below*k, width: 572*k, height: 110*k});
            else if (guide.page === "tools") z.push({x: 24*k, y: (below - 6)*k, width: 572*k, height: 108*k});
            else {
                z.push({x: 24*k, y: (below - 4)*k, width: 572*k, height: 24*k});
                z.push({x: 24*k, y: (below + 80)*k, width: 572*k, height: 27*k});
            }
            return z;
        }

        Item {
            id: content
            anchors.fill: parent; anchors.margins: 38 * guide.s
            component Label: Text {
                color: pal.fg; font.family: guide.theme.fontFamily; font.pixelSize: 16 * guide.s
            }
            Label {
                id: brand; text: "OMAKADE  /  GUIDE"; color: pal.mutedText
                font.pixelSize: 12 * guide.s; font.letterSpacing: 2 * guide.s
            }
            Label {
                anchors.right: parent.right; text: guide.theme.name
                color: pal.mutedText; font.pixelSize: 12 * guide.s
            }
            Item {
                id: header; y: 40 * guide.s; width: parent.width; height: 120 * guide.s
                Image {
                    id: cover; width: 100 * guide.s; height: parent.height
                    source: guide.game.cover || ""; fillMode: Image.PreserveAspectCrop
                }
                Column {
                    x: 122 * guide.s; width: parent.width - x; anchors.verticalCenter: parent.verticalCenter
                    spacing: 10 * guide.s
                    Label { text: (guide.game.source || "").toUpperCase(); color: pal.accent; font.pixelSize: 11 * guide.s; font.letterSpacing: 2 * guide.s }
                    Label { text: guide.game.title || ""; width: parent.width; elide: Text.ElideRight; color: pal.brightFg; font.pixelSize: 30 * guide.s; font.weight: Font.DemiBold }
                    Label { text: "Session  " + guide.game.session; font.pixelSize: 15 * guide.s }
                    Label { text: guide.game.total + " total"; color: pal.mutedText; font.pixelSize: 13 * guide.s }
                }
            }
            Item {
                id: achievements; y: 180 * guide.s; width: parent.width; height: 66 * guide.s
                GuideIcon { name: "trophy"; width: 18 * guide.s; height: width; color: pal.accent }
                Label { x: 30 * guide.s; text: guide.game.achievementsUnlocked + " / " + guide.game.achievementsTotal + " achievements"; font.pixelSize: 14 * guide.s }
                Label { anchors.right: parent.right; text: Math.round(100 * guide.game.achievementsUnlocked / guide.game.achievementsTotal) + "%"; color: pal.mutedText; font.pixelSize: 13 * guide.s }
                Rectangle {
                    y: 30 * guide.s; width: parent.width; height: 3 * guide.s; color: guide.tint(pal.fg, 0.14)
                    Rectangle { height: parent.height; width: parent.width * guide.game.achievementsUnlocked / guide.game.achievementsTotal; color: pal.accent }
                }
                Label { y: 43 * guide.s; text: guide.game.latestAchievement || ""; width: parent.width; elide: Text.ElideRight; color: pal.mutedText; font.pixelSize: 12 * guide.s }
            }
            Rectangle { y: 264 * guide.s; width: parent.width; height: 1; color: guide.tint(pal.fg, 0.14) }
            Label {
                y: 282 * guide.s
                text: guide.confirmingQuit ? "QUIT GAME?" : (guide.page === "home" ? "BACK TO YOUR GAME" : "CONTROLS & MORE")
                color: pal.mutedText; font.pixelSize: 11 * guide.s; font.letterSpacing: 1.8 * guide.s
            }
            Column {
                id: rows; y: 305 * guide.s; width: parent.width
                Repeater {
                    model: guide.actions
                    delegate: GuideRow {
                        required property var modelData
                        required property int index
                        width: rows.width; height: (guide.page === "tools" ? 36 : 43) * guide.s
                        compact: guide.page === "tools"; s: guide.s; radius: guide.radius
                        colors: pal; family: guide.theme.fontFamily
                        label: modelData.label; detail: modelData.detail; icon: modelData.icon
                        order: index + 1; selected: guide.current === index
                        onClicked: { guide.current = index; guide.activate() }
                    }
                }
            }
            Item {
                id: captures; y: rows.y + rows.height + 16 * guide.s
                width: parent.width; height: 100 * guide.s
                visible: guide.page === "home" && !guide.confirmingQuit
                Label { text: "RECENT CAPTURES"; color: pal.mutedText; font.pixelSize: 11 * guide.s; font.letterSpacing: 1.8 * guide.s }
                Row {
                    y: 26 * guide.s; spacing: 12 * guide.s
                    Repeater {
                        model: ["Screenshot · 2m", "Clip · 30s", "Screenshot · 18m"]
                        delegate: Item {
                            id: capture
                            required property string modelData
                            required property int index
                            width: (captures.width - 24 * guide.s) / 3; height: 92 * guide.s
                            Image { width: parent.width; height: 50 * guide.s; source: capture.index === 1 ? "game2.jpg" : guide.backdrop; fillMode: Image.PreserveAspectCrop; clip: true }
                            Rectangle { width: parent.width; height: 50 * guide.s; color: "transparent"; border.width: guide.current === guide.actions.length + capture.index ? 2 : 0; border.color: pal.accent }
                            Label { y: 56 * guide.s; text: (guide.actions.length + capture.index + 1) + "  " + capture.modelData; font.pixelSize: 10 * guide.s; color: pal.mutedText }
                            MouseArea { anchors.fill: parent; onClicked: { guide.current = guide.actions.length + capture.index; guide.activate() } }
                        }
                    }
                }
            }
            Item {
                y: rows.y + rows.height + 16 * guide.s; width: parent.width; height: 130 * guide.s
                visible: guide.page === "tools" && !guide.confirmingQuit
                Label { text: "NOW PLAYING"; color: pal.mutedText; font.pixelSize: 11 * guide.s; font.letterSpacing: 1.8 * guide.s }
                Label { y: 25 * guide.s; text: "Night Drive"; color: pal.brightFg; font.pixelSize: 18 * guide.s }
                Label { y: 48 * guide.s; text: "Chromatic Coast · " + (guide.playing ? "Playing" : "Paused"); color: pal.mutedText; font.pixelSize: 12 * guide.s }
                Label { y: 76 * guide.s; width: parent.width; text: "Note: " + guide.note; elide: Text.ElideRight; color: pal.mutedText; font.pixelSize: 12 * guide.s }
            }
            Label { y: rows.y + rows.height + 26 * guide.s; visible: guide.confirmingQuit; text: "Your game will close. B keeps it running."; width: parent.width; wrapMode: Text.WordWrap }
            Column {
                anchors.bottom: footer.top; anchors.bottomMargin: 22 * guide.s
                width: parent.width; spacing: 12 * guide.s
                Rectangle { width: parent.width; height: 1; color: guide.tint(pal.fg, 0.14) }
                Repeater {
                    model: guide.game.controllers || []
                    delegate: Row {
                        id: controller
                        required property var modelData
                        width: parent.width; spacing: 12 * guide.s
                        GuideIcon { name: "gamepad"; color: pal.fg; width: 20 * guide.s; height: width }
                        Label { text: controller.modelData.name; font.pixelSize: 12 * guide.s; color: pal.mutedText }
                        Label { text: Math.round(controller.modelData.battery * 100) + "%"; font.pixelSize: 12 * guide.s }
                    }
                }
                Label { text: "♪  " + guide.outputs[guide.output] + "  ·  " + Math.round(guide.volume * 100) + "%" + "  ·  Mic " + (guide.micMuted ? "muted" : "on"); font.pixelSize: 12 * guide.s; color: pal.mutedText }
                Label { text: "Display  ·  " + guide.displays[guide.display]; font.pixelSize: 12 * guide.s; color: pal.mutedText }
            }
            Row {
                id: footer; anchors.bottom: parent.bottom; spacing: 20 * guide.s
                Repeater {
                    model: ["↕ Move", "A Select", "B Back", "⌂ Close"]
                    delegate: Label { required property string modelData; text: modelData; font.pixelSize: 12 * guide.s; color: pal.fg }
                }
            }
        }
    }
    GlassSurface {
        x: guide.width - width - 40 * guide.s; y: 40 * guide.s
        width: 286 * guide.s; height: 132 * guide.s
        colors: pal; backdrop: guide.backdrop; alpha: guide.panelAlpha
        radius: guide.radius; scaleFactor: guide.s; frameWidth: guide.width; frameHeight: guide.height
        visible: guide.shown
        Column {
            anchors.centerIn: parent; spacing: 4 * guide.s
            Text { anchors.right: parent.right; text: guide.clock; color: pal.brightFg; font.family: guide.theme.fontFamily; font.pixelSize: 50 * guide.s; font.weight: Font.Light }
            Text { text: guide.date; color: pal.fg; font.family: guide.theme.fontFamily; font.pixelSize: 13 * guide.s }
        }
    }
    GlassSurface {
        x: panel.x + panel.width + 24 * guide.s; y: 360 * guide.s
        width: 570 * guide.s; height: 420 * guide.s
        visible: guide.editingNote && guide.shown
        colors: pal; backdrop: guide.backdrop; alpha: guide.panelAlpha
        radius: guide.radius; scaleFactor: guide.s; frameWidth: guide.width; frameHeight: guide.height
        Column {
            anchors.fill: parent; anchors.margins: 30 * guide.s; spacing: 20 * guide.s
            Text { text: "GAME NOTES"; color: pal.accent; font.family: guide.theme.fontFamily; font.pixelSize: 14 * guide.s }
            Text {
                width: parent.width; height: 74 * guide.s
                text: guide.noteDraft + "▏"; color: pal.fg
                font.family: guide.theme.fontFamily; font.pixelSize: 16 * guide.s; wrapMode: Text.Wrap
            }
            Grid {
                columns: 6; spacing: 6 * guide.s; width: parent.width
                Repeater {
                    model: guide.noteKeys
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: 79 * guide.s; height: 36 * guide.s; radius: guide.radius * 0.5
                        color: "transparent"; border.width: guide.noteCursor === index ? 2 : 1
                        border.color: guide.tint(pal.accent, guide.noteCursor === index ? 1 : 0.2)
                        Text { anchors.centerIn: parent; text: parent.modelData; color: pal.fg; font.family: guide.theme.fontFamily; font.pixelSize: 12 * guide.s }
                    }
                }
            }
            Text { text: "D-pad Move   A Type   B Cancel"; color: pal.mutedText; font.family: guide.theme.fontFamily; font.pixelSize: 13 * guide.s }
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
