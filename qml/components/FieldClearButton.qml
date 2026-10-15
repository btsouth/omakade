import QtQuick

// A clear affordance for a text field, declared as a child of the field it belongs to rather
// than as a wrapper around it. Every field in the app has its own layout, bindings and key
// handling, and replacing them all with one component would have meant rewriting twenty one
// working fields to add one button. This attaches instead: give the field room on the right and
// declare this inside it.
//
// It is reachable with a controller as well as the mouse, so a field can be emptied without a
// keyboard. The field points its right target here; from here, right goes on to wherever the
// field would have gone.
Item {
    id: root

    required property Item field
    // Where right leads once this button has been passed, so the button does not become a dead
    // end in the middle of a row.
    property Item controllerRightTarget: null
    property Item controllerLeftTarget: field
    readonly property bool hasText: root.field !== null && root.field.length > 0

    // The width a field should reserve on its right so text never runs underneath.
    readonly property real reservedWidth: 30

    objectName: (root.field !== null && root.field.objectName !== ""
                 ? root.field.objectName : "field") + "ClearButton"
    Accessible.name: "Clear " + (root.field !== null && root.field.placeholderText !== ""
                                 ? root.field.placeholderText : "text")
    Accessible.role: Accessible.Button

    anchors.right: parent.right
    anchors.rightMargin: 7
    anchors.verticalCenter: parent.verticalCenter
    implicitWidth: 20
    implicitHeight: 20
    width: implicitWidth
    height: implicitHeight
    // An empty field has nothing to clear, and an invisible item is not a focus target, so the
    // button leaves the navigation order entirely rather than becoming a stop that does nothing.
    visible: root.hasText
    // It follows its field into the arrow order rather than joining on its own. On a desktop
    // with a mouse the field is left to the cursor and the caret, and a button that arrows could
    // reach but its own field could not would be a stop with nothing either side of it.
    activeFocusOnTab: root.hasText
                      && (root.field === null || root.field.controllerNavigation !== false)

    function clearField() {
        if (root.field === null) {
            return
        }
        // Move focus first. Clearing hides this item and removes it from the Tab order.
        root.field.forceActiveFocus()
        root.field.clear()
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.activeFocus
               ? Theme.accent
               : pointer.containsMouse
                 ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.18)
                 : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.09)
        border.width: root.activeFocus ? 0 : 1
        border.color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.16)
        Behavior on color {
            enabled: !Preferences.reducedMotion
            ColorAnimation { duration: 110 }
        }
    }

    Text {
        anchors.centerIn: parent
        // A multiplication sign rather than a letter x: it is centred on its own and does not
        // read as text someone might have typed.
        text: "×"
        color: root.activeFocus ? Theme.darkerBackground : Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 13
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clearField()
    }

    function acceptInput(event) { root.clearField(); event.accepted = true }
    function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
    Keys.onReturnPressed: event => acceptInput(event)
    Keys.onEnterPressed: function(event) { root.clearField(); event.accepted = true }
    Keys.onSpacePressed: function(event) { root.clearField(); event.accepted = true }
}
