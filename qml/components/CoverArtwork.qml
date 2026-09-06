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
        source: root.source
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
        Behavior on opacity {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 160 }
        }
    }
}
