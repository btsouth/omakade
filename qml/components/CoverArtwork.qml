import QtQuick

// Cover art for a game card, kept uniform without touching files on disk:
// - art close to the card's shape is stretched the last few percent to fill it
// - square icons and title screens fill the card, cropped
// - genuinely wide box scans are shown whole across the card's width
Item {
    id: root
    property url source
    readonly property int status: artwork.status
    readonly property real frameAspect: height > 0 ? width / height : 2 / 3
    readonly property real imageAspect: artwork.implicitHeight > 0
                                        ? artwork.implicitWidth / artwork.implicitHeight
                                        : frameAspect
    readonly property real shapeRatio: imageAspect / frameAspect
    readonly property bool ready: artwork.status === Image.Ready
    readonly property bool nearFit: ready && shapeRatio >= 0.88 && shapeRatio <= 1.14
    readonly property bool wideArt: ready && shapeRatio >= 1.8

    // Artwork reaches the cover cache as "f/<file path>" or "q/<bundled path>". Marking which
    // kind it is keeps the identifier a plain path, which survives being carried through a URL
    // far more predictably than an escaped one.
    readonly property string cacheId: {
        const text = root.source.toString()
        if (text === "")
            return ""
        if (text.startsWith("qrc:/"))
            return "q" + text.substring(4)
        if (text.startsWith("file://"))
            return "f" + decodeURIComponent(text.substring(7))
        if (text.startsWith("/"))
            return "f" + text
        return ""
    }

    // Wide box scans sit on a plain dark field rather than the card's accent
    // gradient, so the letterbox does not draw the eye.
    Rectangle {
        anchors.fill: parent
        visible: root.wideArt
        color: Theme.darkerBackground
    }

    Image {
        id: artwork
        anchors.fill: parent
        // Wait for the card to have a size before loading. A delegate is created before layout
        // gives it one, so binding straight through decoded every cover twice: once at its full
        // size while the frame was still zero, then again at the size actually wanted.
        //
        // Go through Omakade's own cover cache rather than reading the file directly. Qt only
        // keeps a couple of megabytes of artwork no card is currently showing, about twenty
        // covers, so a library of any size re-read them from disk on every scroll and filter
        // change. The provider keeps a decoded cover for as long as the library is open.
        source: width > 0 && height > 0 && root.cacheId !== "" ? "image://covers/" + root.cacheId : ""
        asynchronous: true
        cache: true
        fillMode: root.nearFit ? Image.Stretch
                : root.wideArt ? Image.PreserveAspectFit
                : Image.PreserveAspectCrop
        // Round the decode size up to 64px steps so window resizes and tiling changes do
        // not reload every visible cover on each step.
        sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 64) * 64
        sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 64) * 64
        opacity: status === Image.Ready ? 1 : 0

        // A cover already decoded is ready within a frame or two, so fading it in is what makes
        // a filter change look like the library is reloading. The fade is kept for art that
        // genuinely has to be read from disk or downloaded.
        property bool fadeIn: false
        Timer {
            id: settleDelay
            interval: 90
            onTriggered: if (artwork.status !== Image.Ready) artwork.fadeIn = true
        }
        Component.onCompleted: settleDelay.start()
        Behavior on opacity {
            enabled: !Preferences.reducedMotion && artwork.fadeIn
            NumberAnimation { duration: 160 }
        }
    }
}
