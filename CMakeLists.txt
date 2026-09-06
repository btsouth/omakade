cmake_minimum_required(VERSION 3.24)

project(Omakade VERSION 1.7.0 LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(LIBZSTD REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

# Wayland idle-inhibit keeps the screensaver away while a launched game runs. Optional so the
# build still works without Wayland headers; the inhibitor becomes a no-op in that case.
pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols)
find_program(WAYLAND_SCANNER wayland-scanner)
set(OMAKADE_IDLE_INHIBIT OFF)
if(WAYLAND_CLIENT_FOUND AND WAYLAND_PROTOCOLS_FOUND AND WAYLAND_SCANNER)
  pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  set(OMAKADE_IDLE_INHIBIT_XML
    "${WAYLAND_PROTOCOLS_DIR}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml")
  if(EXISTS "${OMAKADE_IDLE_INHIBIT_XML}")
    set(OMAKADE_IDLE_INHIBIT ON)
    set(OMAKADE_PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
    file(MAKE_DIRECTORY "${OMAKADE_PROTOCOL_DIR}")
    set(OMAKADE_IDLE_INHIBIT_HEADER
      "${OMAKADE_PROTOCOL_DIR}/idle-inhibit-unstable-v1-client-protocol.h")
    set(OMAKADE_IDLE_INHIBIT_SOURCE "${OMAKADE_PROTOCOL_DIR}/idle-inhibit-unstable-v1-protocol.c")
    add_custom_command(
      OUTPUT "${OMAKADE_IDLE_INHIBIT_HEADER}"
      COMMAND "${WAYLAND_SCANNER}" client-header "${OMAKADE_IDLE_INHIBIT_XML}"
              "${OMAKADE_IDLE_INHIBIT_HEADER}"
      DEPENDS "${OMAKADE_IDLE_INHIBIT_XML}"
      VERBATIM)
    add_custom_command(
      OUTPUT "${OMAKADE_IDLE_INHIBIT_SOURCE}"
      COMMAND "${WAYLAND_SCANNER}" private-code "${OMAKADE_IDLE_INHIBIT_XML}"
              "${OMAKADE_IDLE_INHIBIT_SOURCE}"
      DEPENDS "${OMAKADE_IDLE_INHIBIT_XML}"
      VERBATIM)
  endif()
endif()
message(STATUS "Wayland idle-inhibit: ${OMAKADE_IDLE_INHIBIT}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core Gui Network Qml Quick QuickControls2 Sql Test)
# GuiPrivate exposes the Wayland window native interface used by the idle inhibitor.
find_package(Qt6 6.8 OPTIONAL_COMPONENTS GuiPrivate)
if(OMAKADE_IDLE_INHIBIT AND NOT TARGET Qt6::GuiPrivate)
  message(STATUS "Qt6::GuiPrivate is unavailable; disabling Wayland idle-inhibit")
  set(OMAKADE_IDLE_INHIBIT OFF)
endif()
qt_standard_project_setup(REQUIRES 6.8)

add_library(omakade_core STATIC
  src/backup/BackupArchive.cpp
  src/backup/BackupArchive.h
  src/backup/BackupSnapshot.cpp
  src/backup/BackupSnapshot.h
  src/backup/BackupDatabase.cpp
  src/backup/BackupDatabase.h
  src/backup/BackupRecovery.cpp
  src/backup/BackupRecovery.h
  src/backup/BackupManager.cpp
  src/backup/BackupManager.h
  src/backup/BackupStartup.cpp
  src/backup/BackupStartup.h
  src/achievements/AchievementModel.cpp
  src/achievements/AchievementModel.h
  src/achievements/RetroAchievementsApi.cpp
  src/achievements/RetroAchievementsApi.h
  src/achievements/RetroAchievementsHasher.cpp
  src/achievements/RetroAchievementsHasher.h
  src/achievements/RetroAchievementsService.cpp
  src/achievements/RetroAchievementsService.h
  src/achievements/SteamAchievementApi.cpp
  src/achievements/SteamAchievementApi.h
  src/achievements/SteamAccountService.cpp
  src/achievements/SteamAccountService.h
  src/app/AppSettings.cpp
  src/app/AppSettings.h
  src/app/SingleInstance.cpp
  src/app/SingleInstance.h
  src/input/ControllerFocusGuard.cpp
  src/input/ControllerFocusGuard.h
  src/input/ControllerInput.cpp
  src/input/ControllerInput.h
  src/input/CouchCursorManager.cpp
  src/input/CouchCursorManager.h
  src/library/LibraryFilterModel.cpp
  src/library/LibraryFilterModel.h
  src/library/DatabaseTuning.h
  src/library/GameRoles.h
  src/library/ConsoleCatalog.cpp
  src/library/ConsoleCatalog.h
  src/library/ConsolePortalModel.cpp
  src/library/ConsolePortalModel.h
  src/library/BattleNetGameModel.cpp
  src/library/BattleNetGameModel.h
  src/library/FaugusGameModel.cpp
  src/library/FaugusGameModel.h
  src/library/HeroicGameModel.cpp
  src/library/HeroicGameModel.h
  src/library/LutrisGameModel.cpp
  src/library/LutrisGameModel.h
  src/library/RetroArchGameModel.cpp
  src/library/RetroArchGameModel.h
  src/library/Pcsx2GameModel.cpp
  src/library/Pcsx2GameModel.h
  src/library/RyujinxGameModel.cpp
  src/library/RyujinxGameModel.h
  src/library/Shadps4GameModel.cpp
  src/library/Shadps4GameModel.h
  src/library/CemuGameModel.cpp
  src/library/CemuGameModel.h
  src/library/ManualGameModel.cpp
  src/library/ManualGameModel.h
  src/library/PersonalDataRules.h
  src/library/MockGameModel.cpp
  src/library/MockGameModel.h
  src/library/SteamGameModel.cpp
  src/library/SteamGameModel.h
  src/library/SteamOwnedGamesApi.cpp
  src/library/SteamOwnedGamesApi.h
  src/library/UnifiedGameModel.cpp
  src/library/UnifiedGameModel.h
  src/launch/GameLauncher.cpp
  src/launch/GameLauncher.h
  src/launch/LaunchCommand.h
  src/launch/PlayRequest.cpp
  src/launch/PlayRequest.h
  src/launch/SteamLauncher.cpp
  src/launch/SteamLauncher.h
  src/metadata/GameMetadata.cpp
  src/metadata/GameMetadata.h
  src/metadata/IgdbApi.cpp
  src/metadata/IgdbApi.h
  src/metadata/GameInsightsService.cpp
  src/metadata/GameInsightsService.h
  src/sources/FlatpakInstall.h
  src/sources/steam/SteamScanner.cpp
  src/sources/steam/SteamScanner.h
  src/sources/steam/ValveKeyValues.cpp
  src/sources/steam/ValveKeyValues.h
  src/sources/lutris/LutrisScanner.cpp
  src/sources/lutris/LutrisScanner.h
  src/sources/retroarch/RetroArchScanner.cpp
  src/sources/retroarch/RetroArchScanner.h
  src/sources/retro/RomFolderScanner.cpp
  src/sources/retro/RomFolderScanner.h
  src/sources/pcsx2/Pcsx2Scanner.cpp
  src/sources/pcsx2/Pcsx2Scanner.h
  src/sources/ryujinx/RyujinxScanner.cpp
  src/sources/ryujinx/RyujinxScanner.h
  src/sources/shadps4/Shadps4Scanner.cpp
  src/sources/shadps4/Shadps4Scanner.h
  src/sources/cemu/CemuScanner.cpp
  src/sources/cemu/CemuScanner.h
  src/sources/dolphin/DolphinScanner.cpp
  src/sources/dolphin/DolphinScanner.h
  src/library/DolphinGameModel.cpp
  src/library/DolphinGameModel.h
  src/artwork/SwitchTitleReader.cpp
  src/artwork/SwitchTitleReader.h
  src/artwork/ZArchiveReader.cpp
  src/artwork/ZArchiveReader.h
  src/artwork/TgaImage.cpp
  src/artwork/TgaImage.h
  src/sources/heroic/HeroicScanner.cpp
  src/sources/heroic/HeroicScanner.h
  src/sources/faugus/FaugusScanner.cpp
  src/sources/faugus/FaugusScanner.h
  src/sources/battlenet/BattleNetScanner.cpp
  src/sources/battlenet/BattleNetScanner.h
  src/streaming/SunshineIntegration.cpp
  src/streaming/SunshineIntegration.h
  src/theme/OmarchyTheme.cpp
  src/theme/OmarchyTheme.h
)

target_include_directories(omakade_core PUBLIC src)
target_link_libraries(omakade_core PUBLIC
  PkgConfig::SDL3
  PkgConfig::LIBSECRET
  PkgConfig::LIBZIP
  PkgConfig::LIBZSTD
  OpenSSL::Crypto
  Qt6::Concurrent
  Qt6::Core
  Qt6::Gui
  Qt6::Network
  Qt6::Sql
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(omakade
  src/app/main.cpp
  src/app/IdleInhibitor.cpp
  src/app/IdleInhibitor.h
)
if(OMAKADE_IDLE_INHIBIT)
  target_sources(omakade PRIVATE "${OMAKADE_IDLE_INHIBIT_HEADER}" "${OMAKADE_IDLE_INHIBIT_SOURCE}")
  set_source_files_properties("${OMAKADE_IDLE_INHIBIT_SOURCE}" PROPERTIES
    COMPILE_OPTIONS "-w")
  target_include_directories(omakade PRIVATE "${OMAKADE_PROTOCOL_DIR}")
  target_compile_definitions(omakade PRIVATE OMAKADE_IDLE_INHIBIT=1)
  target_link_libraries(omakade PRIVATE PkgConfig::WAYLAND_CLIENT Qt6::GuiPrivate)
endif()

qt_add_qml_module(omakade
  URI Omakade
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/components/CoverSizeControl.qml
    qml/components/CoverArtwork.qml
    qml/components/GameCard.qml
    qml/components/CouchLibraryView.qml
    qml/components/CouchKeyboard.qml
    qml/components/CouchBrowsePanel.qml
    qml/components/GameMetadataEditor.qml
    qml/components/SettingsPanel.qml
    qml/components/GlassButton.qml
    qml/components/LibraryView.qml
    qml/screens/ArtworkEditor.qml
    qml/screens/RestoreStartup.qml
    qml/screens/BackupEditor.qml
    qml/screens/SavedFiltersEditor.qml
    qml/screens/BulkOrganizationEditor.qml
    qml/screens/ManualGameEditor.qml
    qml/screens/GameDetails.qml
)

qt_add_resources(omakade "omakade_icons"
  PREFIX "/icons"
  FILES
    resources/icons/io.github.tsouth89.Omakade.svg
)

# Console card art: any PNG, JPG, or SVG dropped into resources/consoles named
# after a ConsoleCatalog id (snes.png, n64.svg, ...) is bundled automatically.
file(GLOB OMAKADE_CONSOLE_ART CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/consoles/*.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/consoles/*.jpg"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/consoles/*.svg")
qt_add_resources(omakade "omakade_consoles"
  PREFIX "/consoles"
  BASE "resources/consoles"
  FILES ${OMAKADE_CONSOLE_ART}
)

set_target_properties(omakade PROPERTIES
  OUTPUT_NAME omakade
  MACOSX_BUNDLE_GUI_IDENTIFIER io.github.tsouth89.Omakade
  WIN32_EXECUTABLE TRUE
)

target_compile_definitions(omakade PRIVATE
  OMAKADE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(omakade PRIVATE
  omakade_core
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS omakade
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES packaging/io.github.tsouth89.Omakade.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES resources/icons/io.github.tsouth89.Omakade.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
install(FILES packaging/io.github.tsouth89.Omakade.metainfo.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
install(FILES LICENSE COPYRIGHT
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/omakade
)
install(FILES resources/consoles/ARTWORK.md resources/consoles/GENERATION.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/omakade
)
install(FILES README.md PRIVACY.md SUPPORT.md CHANGELOG.md docs/COMPATIBILITY.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omakade
)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
