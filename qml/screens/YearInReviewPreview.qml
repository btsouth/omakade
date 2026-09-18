import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// The preview of the shareable card, and the one action that matters: write it out.
//
// The card is laid out at its own size and shown scaled to fit, and it is grabbed at that same
// size, so what the player approves here is the shape of the file that lands on disk rather than a
// window-sized approximation of it.
FocusScope {
    id: root
    objectName: "yearInReviewPreview"
    property bool couchMode: false
    readonly property real scaleFactor: couchMode ? 1.2 : 1
    property string status: ""
    property string savedPath: ""
    property string failure: ""
    signal closed()

    function open() {
        status = ""
        savedPath = ""
        failure = ""
        visible = true
        forceActiveFocus(Qt.TabFocusReason)
        Qt.callLater(function() {
            saveButton.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    function close() {
        visible = false
        root.closed()
    }
    // Writes the card to `path`, or to the standard location when no path is given. The grab is
    // asynchronous, so the outcome is reported in the callback.
    function saveTo(path) {
        const target = (path && path.length > 0) ? path : CardExport.pathFor(Stats.periodLabel)
        if (target.length === 0) {
            failure = "No pictures folder could be found to write the card into."
            return
        }
        // One turn later: the card may have just been made visible, and a grab taken before the
        // layout has settled renders an item that is not there yet.
        Qt.callLater(function() {
            card.grabToImage(function(result) {
                if (result.saveToFile(target)) {
                    root.savedPath = target
                    root.status = "Saved to " + target
                    CardExport.reportExport(true)
                } else {
                    root.failure = "The card could not be written to " + target
                    CardExport.reportExport(false)
                }
            }, Qt.size(card.cardWidth, card.cardHeight))
        })
    }

    Keys.onEscapePressed: function(event) {
        root.close()
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.78)
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24 * root.scaleFactor
        spacing: 12 * root.scaleFactor

        RowLayout {
            Layout.fillWidth: true
            spacing: 10 * root.scaleFactor
            Text {
                Layout.fillWidth: true
                text: "YOUR CARD"
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: 13 * root.scaleFactor
                font.letterSpacing: 1.4 * root.scaleFactor
                font.weight: Font.DemiBold
            }
            GlassButton {
                id: saveButton
                objectName: "cardSaveButton"
                text: "SAVE IMAGE"
                compact: true
                onClicked: root.saveTo("")
                // The folder button only exists after a save, so the chain steps over it until it
                // does: a controller should never be left pressing right into nothing.
                KeyNavigation.right: cardFolderButton.visible ? cardFolderButton : cardCloseButton
            }
            GlassButton {
                id: cardFolderButton
                objectName: "cardFolderButton"
                text: "OPEN FOLDER"
                compact: true
                visible: root.savedPath.length > 0
                onClicked: CardExport.reveal(root.savedPath)
                KeyNavigation.left: saveButton
                KeyNavigation.right: cardCloseButton
            }
            GlassButton {
                id: cardCloseButton
                objectName: "cardCloseButton"
                text: "CLOSE"
                compact: true
                onClicked: root.close()
                KeyNavigation.left: cardFolderButton.visible ? cardFolderButton : saveButton
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Item {
                id: cardHost
                width: card.cardWidth
                height: card.cardHeight
                anchors.centerIn: parent
                scale: Math.min(1, parent.width / width, parent.height / height)
                transformOrigin: Item.Center
                YearInReviewCard {
                    id: card
                    anchors.fill: parent
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.status.length > 0 || root.failure.length > 0
            wrapMode: Text.Wrap
            text: root.failure.length > 0 ? root.failure : root.status
            color: root.failure.length > 0 ? Theme.red : Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 11 * root.scaleFactor
        }
    }
}
