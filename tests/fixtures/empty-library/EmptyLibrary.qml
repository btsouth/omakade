import QtQuick

// Copied next to the components by EmptyLibraryUiTests, so they resolve from the same folder.
// The library grid with no games in it, as on a first run.
Rectangle {
    width: 900
    height: 560
    color: Theme.darkerBackground

    ListModel { id: noGames }

    LibraryView {
        objectName: "libraryView"
        anchors.fill: parent
        libraryModel: noGames
        setupAvailable: true
        emptyTitle: "Set up your library"
        emptyMessage: "Omakade finds games in your launchers, emulators and ROM folders. Choose your sources in Settings, then rescan."
    }
}
