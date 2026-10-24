import QtQuick

QtObject {
    id: root
    property var request: ({})
    property bool pending: false
    property bool failed: false
    property string message: ""
    // The launch went out and the game has not opened a window yet.
    property bool waiting: false
    property int waitTick: 0
    property bool reducedMotion: false
    readonly property string busyText: waiting
        ? "STARTING" + (reducedMotion ? "..." : ".".repeat(waitTick % 4)) : "OPENING..."
    signal dispatchRequested(var request)

    function begin(value) {
        if (pending) return false
        // Selection can change before dispatch. Keep the installation the user chose.
        request = JSON.parse(JSON.stringify(value))
        failed = false
        message = "Opening " + request.title + "..."
        pending = true
        dispatch.start()
        return true
    }

    // Stays busy after a launch that went out, until done(), for launchers that show
    // nothing while their game starts.
    function wait(text) {
        cooldown.stop()
        pending = true
        failed = false
        waiting = true
        waitTick = 0
        message = text
    }

    function done() {
        if (!waiting) return
        waiting = false
        pending = false
        message = ""
    }

    function finish(success, text) {
        dispatch.stop()
        cooldown.stop()
        pending = success
        failed = !success
        message = text
        if (success) cooldown.start()
        else pending = false
    }

    // Give the pressed button and status text a frame to appear before starting
    // the external launcher. Repeated keyboard/controller presses are ignored.
    property Timer dispatch: Timer {
        interval: 50
        onTriggered: root.dispatchRequested(root.request)
    }
    property Timer waitPulse: Timer {
        interval: 450
        repeat: true
        running: root.waiting && !root.reducedMotion
        onTriggered: root.waitTick++
    }
    property Timer cooldown: Timer {
        interval: 2000
        onTriggered: { root.pending = false; root.message = "" }
    }
}
