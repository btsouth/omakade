import QtQuick
import QtQuick.Window
import "themes.js" as Themes

// Design preview for the in-game guide. Not part of the app.
//   qml Preview.qml -- --theme=osaka-jade --variant=steam|emulator --closed --cycle
// Keys: Up/Down/Enter/Esc drive the guide, G toggles it, T cycles themes.
Window {
    id: win
    width: 1920
    height: 1080
    visible: true
    visibility: Window.FullScreen
    color: "black"
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
        property string fontFamily: "JetBrainsMono Nerd Font"
        property int cornerRadius: parseInt(win.arg("radius", "10"))
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
        theme: theme
        game: win.games[win.variant] || win.games.steam
        backdrop: gameFrame.source
        shown: win.arg("closed", "") === ""
        current: parseInt(win.arg("focus", "0"))
        onCloseRequested: shown = false
    }
    Shortcut { sequence: "G"; onActivated: guide.shown = !guide.shown }
    Shortcut { sequence: "T"; onActivated: win.themeIndex = (win.themeIndex + 1) % win.slugs.length }

    Timer {
        running: win.arg("cycle", "") !== ""
        interval: 2500
        repeat: true
        onTriggered: win.themeIndex = (win.themeIndex + 1) % win.slugs.length
    }
}
