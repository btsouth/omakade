import QtQuick
import QtQuick.Window

FocusScope {
    id: root

    required property string title
    required property string subtitle
    required property int hours
    // IGDB score out of 100. Below zero means this game has no rating, and the card
    // then shows nothing rather than a placeholder.
    required property int rating
    required property int progress
    required property bool favorite
    required property string completionStatus
    required property color accentStart
    required property color accentEnd
    required property string coverMark
    required property string coverPath
    property bool current: false

    signal activated()
    signal favoriteToggled()

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    activeFocusOnTab: true
    Accessible.name: title
    Accessible.description: subtitle + ", " + hours + " hours played"
                            + (rating >= 0 ? ", rated " + rating + " out of 100" : "")
    Accessible.role: Accessible.ListItem

    Keys.onReturnPressed: function(event) {
        root.activated()
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        root.activated()
        event.accepted = true
    }
    Keys.onSpacePressed: function(event) {
        root.activated()
        event.accepted = true
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_F) {
            root.favoriteToggled()
            event.accepted = true
        }
    }

    scale: current ? 1.018 : cardMouse.containsMouse ? 1.01 : 1.0
    Behavior on scale {
        enabled: !Preferences.reducedMotion
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.fill: cover
        anchors.margins: root.current ? -5 : 0
        radius: cover.radius + 4
        color: root.current ? root.alpha(Theme.accent, 0.16) : "transparent"
        border.width: root.current ? 2 : 0
        border.color: Theme.accent
    }

    Rectangle {
        anchors.fill: cover
        anchors.topMargin: 6
        anchors.leftMargin: 5
        anchors.rightMargin: -5
        anchors.bottomMargin: -6
        radius: cover.radius
        color: root.alpha(Theme.darkerBackground, 0.34)
    }

    Rectangle {
        id: cover
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.round(width * 1.5)
        radius: Math.max(5, Theme.cornerRadius)
        clip: true
        border.width: root.current ? 3 : 1
        border.color: root.current ? Theme.accent : root.alpha(Theme.foreground, 0.15)

        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: root.accentStart }
            GradientStop { position: 1.0; color: root.accentEnd }
        }

        CoverArtwork {
            id: artwork
            anchors.fill: parent
            source: root.coverPath
        }

        Rectangle {
            visible: artwork.status !== Image.Ready
            width: cover.width * 0.9
            height: width
            radius: width / 2
            x: cover.width * 0.46
            y: -height * 0.22
            color: root.alpha(Theme.brightForeground, 0.10)
            border.color: root.alpha(Theme.brightForeground, 0.16)
        }

        Rectangle {
            visible: artwork.status !== Image.Ready
            width: cover.width * 0.7
            height: width
            radius: width / 2
            x: -width * 0.38
            y: cover.height * 0.38
            color: root.alpha(Theme.darkerBackground, 0.22)
        }

        Text {
            visible: artwork.status !== Image.Ready
            anchors.centerIn: parent
            text: root.coverMark
            color: root.alpha(Theme.brightForeground, 0.88)
            font.family: Theme.fontFamily
            font.pixelSize: Math.max(38, cover.width * 0.32)
            font.weight: Font.Light
        }

        // The caption under the card already names the game; the overlay only
        // labels cards that have no cover art to show.
        Rectangle {
            visible: artwork.status !== Image.Ready
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * 0.42
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: root.alpha(Theme.darkerBackground, 0.84) }
            }
        }

        Column {
            visible: artwork.status !== Image.Ready
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 13
            spacing: 5

            Text {
                width: parent.width
                text: root.title.toUpperCase()
                textFormat: Text.PlainText
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Math.max(12, cover.width * 0.078)
                font.weight: Font.Bold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Rectangle {
                width: parent.width
                height: 2
                radius: 1
                color: root.alpha(Theme.brightForeground, 0.28)

                Rectangle {
                    width: parent.width * root.progress / 100
                    height: parent.height
                    radius: 1
                    color: Theme.brightForeground
                }
            }
        }

        Rectangle {
            visible: root.completionStatus.length > 0
            height: 25
            width: statusText.implicitWidth + 18
            radius: 13
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 9
            color: root.completionStatus === "completed"
                   ? root.alpha(Theme.green, 0.82)
                   : root.completionStatus === "playing"
                     ? root.alpha(Theme.accent, 0.82)
                     : root.completionStatus === "backlog"
                       ? root.alpha(Theme.yellow, 0.78)
                       : root.alpha(Theme.darkerBackground, 0.72)
            border.color: root.alpha(Theme.brightForeground, 0.20)

            Text {
                id: statusText
                anchors.centerIn: parent
                text: root.completionStatus.toUpperCase()
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: 8
                font.weight: Font.Bold
            }
        }

        Rectangle {
            visible: root.favorite
            width: 28
            height: 28
            radius: 14
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 9
            color: root.alpha(Theme.darkerBackground, 0.66)
            border.color: root.alpha(Theme.brightForeground, 0.22)

            Text {
                anchors.centerIn: parent
                text: "♥"
                color: Theme.brightForeground
                font.pixelSize: 12
            }
        }

        Behavior on border.color {
            enabled: !Preferences.reducedMotion
            ColorAnimation { duration: 120 }
        }
    }

    Column {
        anchors.top: cover.bottom
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 3

        Text {
            width: parent.width
            text: root.title
            // Titles come from launcher data, so never let one render as markup.
            textFormat: Text.PlainText
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Row {
            id: metaRow
            width: parent.width
            spacing: 7
            // The source name gives up whatever room the fixed trailing fields need, so a long
            // name elides instead of pushing the playtime or the rating off the card.
            readonly property real trailingWidth:
                subtitleDot.width + subtitleHours.width + spacing * 2
                + (subtitleRating.visible
                   ? subtitleRatingDot.width + subtitleRating.width + spacing * 2 : 0)

            Text {
                width: Math.min(implicitWidth, Math.max(0, parent.width - metaRow.trailingWidth))
                elide: Text.ElideRight
                text: root.subtitle
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 10
            }
            Text {
                id: subtitleDot
                text: "·"
                color: root.alpha(Theme.foreground, 0.32)
                font.pixelSize: 10
            }
            Text {
                id: subtitleHours
                text: root.hours + "h"
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 10
            }
            Text {
                id: subtitleRatingDot
                visible: subtitleRating.visible
                text: "·"
                color: root.alpha(Theme.foreground, 0.32)
                font.pixelSize: 10
            }
            Text {
                id: subtitleRating
                objectName: "cardRating"
                visible: root.rating >= 0
                text: root.rating + "%"
                // Brighter than the rest of the line, so a rating-sorted grid can be read
                // down the column at a glance.
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }
    }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus()
            root.activated()
        }
    }
}
