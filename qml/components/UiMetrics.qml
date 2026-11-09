pragma Singleton
import QtQuick

QtObject {
    // Settings > Appearance > Text size on the desktop, as a factor. The window sets it, and
    // keeps it at 1 in Game Mode, which scales its own text to the display instead.
    property real textScale: 1
    function scaled(size) { return Math.round(size * textScale) }

    // Shared type and spacing roles. Screens multiply these by their couch scale.
    // Caption is for short annotations on cards and badges, never for sentences.
    readonly property int caption: scaled(11)
    readonly property int body: scaled(14)
    readonly property int supporting: scaled(12)
    readonly property int label: scaled(12)
    readonly property int section: scaled(18)
    readonly property int heading: scaled(24)
    readonly property int controlHeight: scaled(42)
    readonly property int compactControlHeight: scaled(36)
    readonly property int gap: 12
    readonly property int sectionGap: 22
}
