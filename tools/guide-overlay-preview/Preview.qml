import QtQuick
import QtQuick.Window
import "themes.js" as Themes
import "Contrast.js" as Contrast

// Design preview for the in-game guide. Not part of the app.
//   qml Preview.qml -- --theme=osaka-jade --variant=steam|emulator --closed --cycle
// Keys: Up/Down/Enter/Esc drive the guide, G toggles it, T cycles themes.
Window {
    id: win
    width: 1920
    height: 1080
    visible: true
    visibility: Window.FullScreen
    color: theme.background
    title: "Guide overlay preview"

    function arg(name, fallback) {
        const args = Qt.application.arguments
        for (let i = 0; i < args.length; ++i) {
            if (args[i] === "--" + name) return "1"
            if (args[i].startsWith("--" + name + "=")) return args[i].slice(name.length + 3)
        }
        return fallback
    }

    readonly property var slugs: Object.keys(Themes.themes)
    property int themeIndex: Math.max(0, slugs.indexOf(arg("theme", "osaka-jade")))

    QtObject {
        id: theme
        readonly property var t: Themes.themes[win.slugs[win.themeIndex]]
        property string name: t.name
        property string mode: t.mode
        property string fontFamily: win.arg("font", "monospace")
        property int cornerRadius: parseInt(win.arg("radius", "0"))
        property color accent: t.accent
        property color selection: t.selection
        property color background: t.background
        property color darkBackground: t.darkbackground
        property color darkerBackground: t.darkerbackground
        property color lighterBackground: t.lighterbackground
        property color foreground: t.foreground
        property color brightForeground: t.brightforeground
        property color mutedText: t.mutedtext
        property color red: t.red
        property color green: t.green
        property color yellow: t.yellow
    }

    readonly property string variant: arg("variant", "steam")
    readonly property var games: ({
        "steam": {
            title: "Lantern Road", slug: "lantern-road", source: "Steam", steam: true,
            cover: "cover.jpg", session: "42 min", total: "31 h 20 min",
            achievementsUnlocked: 23, achievementsTotal: 63,
            latestAchievement: "Night Market Regular · 12 min ago",
            controllers: [{ name: "Xbox Wireless Controller", battery: 0.75 }],
            output: "Living room TV"
        },
        "emulator": {
            title: "Lantern Road Advance", slug: "lantern-road-advance", source: "RetroArch · GBA",
            saveBackup: true, lastBackup: "2 h ago",
            cover: "cover.jpg", session: "18 min", total: "6 h 05 min",
            achievementsUnlocked: 9, achievementsTotal: 40,
            latestAchievement: "First Ferry · RetroAchievements",
            controllers: [{ name: "8BitDo Ultimate", battery: 0.4 }, { name: "DualSense", battery: 0.2 }],
            output: "Headphones"
        }
    })

    Image {
        id: gameFrame
        anchors.fill: parent
        source: win.arg("backdrop", "game.jpg")
        fillMode: Image.PreserveAspectCrop
    }

    GuideOverlay {
        id: guide
        anchors.fill: parent
        focus: true
        opacity: 1 - previous.opacity
        theme: theme
        game: win.games[win.variant] || win.games.steam
        backdrop: gameFrame.source
        shown: win.arg("closed", "") === ""
        current: parseInt(win.arg("focus", "0"))
        page: win.arg("page", "home")
        onCloseRequested: shown = false
    }
    Shortcut { sequence: "G"; onActivated: guide.shown = !guide.shown }
    // Freeze the complete old guide, then cross-fade to the new theme in 0.5 s.
    // Do not interpolate light/dark palette colours into an unaudited intermediate theme.
    property bool switching: false
    property var frozenGrab
    function nextTheme() {
        if (switching) return;
        switching = true;
        guide.grabToImage(result => {
            win.frozenGrab = result;
            previous.source = result.url;
            previous.opacity = 1;
            win.themeIndex = (win.themeIndex + 1) % win.slugs.length;
            fade.restart();
        });
    }
    Image { id: previous; anchors.fill: parent; opacity: 0; visible: opacity > 0 }
    NumberAnimation { id: fade; target: previous; property: "opacity"; from: 1; to: 0; duration: 500; easing.type: Easing.InOutQuad; onFinished: win.switching = false }
    Shortcut { sequence: "T"; onActivated: win.nextTheme() }
    Component.onCompleted: {
        if (win.arg("audit", "") !== "") {
            console.log("CONTRAST_AUDIT " + JSON.stringify(slugs.map(slug => Contrast.audit(Themes.themes[slug]))));
            Qt.quit();
        }
    }

    // Export only inside an isolated desktop. Each capture follows a painted frame;
    // changing the index schedules the next frame, with no arbitrary render sleeps.
    property bool exportBusy: false
    property bool exportStarted: false
    Connections {
        target: win
        function onFrameSwapped() {
            if (win.arg("export", "") === "" || win.exportBusy) return;
            win.exportBusy = true;
            if (!win.exportStarted) { win.exportStarted = true; win.themeIndex = 0; win.exportBusy = false; return }
            win.contentItem.grabToImage(result => {
                const path = win.arg("export", "") + "/" + win.slugs[win.themeIndex] + ".png";
                if (!result.saveToFile(path)) { console.error("EXPORT_FAILED", path); Qt.exit(1); return }
                console.log("EXPORTED", win.slugs[win.themeIndex]);
                if (win.themeIndex === win.slugs.length - 1) { console.log("EXPORT_DONE"); Qt.quit(); return }
                ++win.themeIndex;
                win.exportBusy = false;
            });
        }
    }

    Timer {
        running: win.arg("cycle", "") !== ""
        interval: 2500
        repeat: true
        onTriggered: win.nextTheme()
    }
}
