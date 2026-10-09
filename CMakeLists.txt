cmake_minimum_required(VERSION 3.24)

project(Omakade VERSION 1.16.0 LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
# Puts the Game Mode controls on a layer-shell overlay above a fullscreen game.
find_package(LayerShellQt REQUIRED)
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

find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core DBus Gui Network Qml Quick QuickControls2 Sql Test)
# GuiPrivate exposes the Wayland window native interface used by the idle inhibitor.
find_package(Qt6 6.8 OPTIONAL_COMPONENTS GuiPrivate)
if(OMAKADE_IDLE_INHIBIT AND NOT TARGET Qt6::GuiPrivate)
  message(STATUS "Qt6::GuiPrivate is unavailable; disabling Wayland idle-inhibit")
  set(OMAKADE_IDLE_INHIBIT OFF)
endif()
qt_standard_project_setup(REQUIRES 6.8)

# Play session tracking, shared by the Omakade window and the omakade-sessiond
# recorder. Kept separate so the daemon links only Qt Core, Sql, and Network.
add_library(omakade_tracking STATIC
  src/tracking/AppNotify.cpp
  src/tracking/AppNotify.h
  src/tracking/AttributionAdapter.cpp
  src/tracking/AttributionAdapter.h
  src/tracking/DiscordPresence.cpp
  src/tracking/DiscordPresence.h
  src/tracking/HyprlandWindows.cpp
  src/tracking/HyprlandWindows.h
  src/tracking/GameStop.cpp
  src/tracking/GameStop.h
  src/tracking/GameStopper.cpp
  src/tracking/GameStopper.h
  src/tracking/PlaySessionStore.cpp
  src/tracking/PlaySessionStore.h
  src/tracking/ProcFs.cpp
  src/tracking/ProcFs.h
  src/tracking/ProcessMatcher.cpp
  src/tracking/ProcessMatcher.h
  src/tracking/SessionDatabase.cpp
  src/tracking/SessionDatabase.h
  src/tracking/SessionDisplay.cpp
  src/tracking/SessionDisplay.h
  src/tracking/SessionJournal.cpp
  src/tracking/SessionJournal.h
  src/tracking/SessionRecorder.cpp
  src/tracking/SessionRecorder.h
  src/tracking/SessionStopper.cpp
  src/tracking/SessionStopper.h
  src/tracking/SessionTitleIndex.cpp
  src/tracking/SessionTitleIndex.h
)

target_include_directories(omakade_tracking PUBLIC src)
# The recorder reads this file, and so does the stop path when it has to claim
# an emulator session, so the path is defined once here rather than per binary.
target_compile_definitions(omakade_tracking PRIVATE
  OMAKADE_SESSIOND_PROFILES="${CMAKE_INSTALL_FULL_DATADIR}/omakade/sessiond-profiles.json"
)
target_link_libraries(omakade_tracking PUBLIC
  Qt6::Core
  Qt6::Network
  Qt6::Sql
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade_tracking PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(omakade-sessiond src/sessiond/main.cpp)
target_link_libraries(omakade-sessiond PRIVATE omakade_tracking)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade-sessiond PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The controller Guide button listener. Qt Core only, and read-only access to evdev.
add_library(omakade_guide STATIC
  src/guidebutton/GuideListener.cpp
  src/guidebutton/GuideListener.h
  src/guidebutton/GuidePress.cpp
  src/guidebutton/GuidePress.h
)
target_include_directories(omakade_guide PUBLIC src)
target_link_libraries(omakade_guide PUBLIC Qt6::Core)

qt_add_executable(omakade-guide-button src/guidebutton/main.cpp)
target_link_libraries(omakade-guide-button PRIVATE omakade_guide Qt6::Network)
target_sources(omakade-guide-button PRIVATE src/guide/GuideClient.cpp src/guide/GuideClient.h)
target_compile_definitions(omakade-guide-button PRIVATE OMAKADE_VERSION="${PROJECT_VERSION}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade_guide PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(omakade-guide-button PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(omakade_guide_runtime STATIC
  src/guide/GuideArt.cpp src/guide/GuideArt.h
  src/guide/GuidePayload.cpp src/guide/GuidePayload.h
  src/guide/GuideInput.cpp src/guide/GuideInput.h
  src/guide/GuidePause.cpp src/guide/GuidePause.h
  src/guide/GuideAnr.cpp src/guide/GuideAnr.h
  src/guide/GuideActions.cpp src/guide/GuideActions.h
)
target_include_directories(omakade_guide_runtime PUBLIC src)
target_link_libraries(omakade_guide_runtime PUBLIC omakade_guide omakade_tracking Qt6::Gui Qt6::Network Qt6::Concurrent)
qt_add_executable(omakade-guide-guard src/guide/guard.cpp)
target_link_libraries(omakade-guide-guard PRIVATE omakade_guide_runtime)

add_library(omakade_core STATIC
  src/backup/BackupArchive.cpp
  src/backup/BackupArchive.h
  src/backup/BackupSnapshot.cpp
  src/backup/BackupSnapshot.h
  src/backup/BackupDatabase.cpp
  src/backup/BackupDatabase.h
  src/backup/BackupRecovery.cpp
  src/backup/BackupRecovery.h
  src/saves/SaveManagement.cpp
  src/saves/SaveProtection.cpp
  src/saves/SaveProtection.h
  src/saves/SaveBackups.cpp
  src/saves/SaveSetStore.cpp
  src/saves/SaveLayouts.cpp
  src/saves/SaveBackups.h
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
  src/library/HomeModel.cpp
  src/library/HomeModel.h
  src/library/LibraryFilterModel.cpp
  src/library/LibraryFilterModel.h
  src/library/DatabaseTuning.h
  src/library/GameRoles.h
  src/library/GameStopService.cpp
  src/library/GameStopService.h
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
  src/library/RommGameModel.cpp
  src/library/RommGameModel.h
  src/library/CemuGameModel.cpp
  src/library/CemuGameModel.h
  src/library/XeniaGameModel.cpp
  src/library/XeniaGameModel.h
  src/library/ManualGameModel.cpp
  src/library/ManualGameModel.h
  src/library/PersonalDataRules.h
  src/library/PlayStats.cpp
  src/library/PlayStats.h
  src/library/MockGameModel.cpp
  src/library/MockGameModel.h
  src/library/SteamGameModel.cpp
  src/library/SteamGameModel.h
  src/library/SteamOwnedGamesApi.cpp
  src/library/SteamOwnedGamesApi.h
  src/library/UnifiedGameModel.cpp
  src/library/ReviewAvailability.cpp
  src/library/ReviewAvailability.h
  src/library/LibraryRepair.cpp
  src/library/ReviewUndo.cpp
  src/library/UnifiedGameModel.h
  src/sources/romm/RommCredentials.cpp
  src/sources/romm/RommCredentials.h
  src/sources/romm/RommCatalog.cpp
  src/sources/romm/RommCatalog.h
  src/sources/romm/RommScanner.cpp
  src/sources/romm/RommScanner.h
  src/launch/GameLaunchSetup.cpp
  src/launch/GameLauncher.cpp
  src/launch/GameLauncher.h
  src/launch/LaunchCommand.h
  src/launch/PlayRequest.cpp
  src/launch/PlayRequest.h
  src/launch/SteamLauncher.cpp
  src/launch/SteamLauncher.h
  src/metadata/ProtonDbService.cpp
  src/metadata/ProtonDbService.h
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
  src/sources/xenia/XeniaScanner.cpp
  src/sources/xenia/XeniaScanner.h
  src/sources/dolphin/DolphinScanner.cpp
  src/sources/dolphin/DolphinScanner.h
  src/sources/melonds/MelondsScanner.cpp
  src/sources/melonds/MelondsScanner.h
  src/sources/rpcs3/Rpcs3Scanner.cpp
  src/sources/rpcs3/Rpcs3Scanner.h
  src/sources/ppsspp/PpssppScanner.cpp
  src/sources/ppsspp/PpssppScanner.h
  src/library/DolphinGameModel.cpp
  src/library/DolphinGameModel.h
  src/library/MelondsGameModel.cpp
  src/library/MelondsGameModel.h
  src/library/Rpcs3GameModel.cpp
  src/library/Rpcs3GameModel.h
  src/library/PpssppGameModel.cpp
  src/library/PpssppGameModel.h
  src/artwork/SwitchTitleReader.cpp
  src/artwork/SwitchTitleReader.h
  src/artwork/ZArchiveReader.cpp
  src/artwork/ZArchiveReader.h
  src/artwork/TgaImage.cpp
  src/artwork/TgaImage.h
  src/artwork/CoverImageProvider.cpp
  src/artwork/CoverImageProvider.h
  src/sources/heroic/HeroicScanner.cpp
  src/sources/heroic/HeroicScanner.h
  src/sources/faugus/FaugusScanner.cpp
  src/sources/faugus/FaugusScanner.h
  src/sources/battlenet/BattleNetScanner.cpp
  src/sources/battlenet/BattleNetScanner.h
  src/gamemode/GameModeController.cpp
  src/gamemode/GameModeController.h
  src/gamemode/GameModeDesktop.cpp
  src/gamemode/GameModeDesktop.h
  src/gamemode/GameModeGuideButton.cpp
  src/gamemode/GameModeGuideButton.h
  src/guide/GuidePlugin.cpp
  src/guide/GuidePlugin.h
  src/guide/InGameGuide.cpp
  src/guide/InGameGuide.h
  src/guide/GuideClient.cpp src/guide/GuideClient.h
  src/guide/ResidentGuide.cpp src/guide/ResidentGuide.h
  src/guide/GuideEnvironment.cpp src/guide/GuideEnvironment.h
  src/gamemode/GameModeOverlay.cpp
  src/gamemode/GameModeOverlay.h
  src/gamemode/GameModePorts.h
  src/gamemode/GameModeSession.cpp
  src/gamemode/GameModeSession.h
  src/gamemode/GameModeShortcut.cpp
  src/gamemode/GameModeShortcut.h
  src/streaming/SunshineIntegration.cpp
  src/streaming/SunshineIntegration.h
  src/theme/OmarchyTheme.cpp
  src/theme/OmarchyTheme.h
)

target_include_directories(omakade_core PUBLIC src)
target_link_libraries(omakade_core PUBLIC
  omakade_guide_runtime
  omakade_tracking
  LayerShellQt::Interface
  Qt6::Quick
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

target_link_libraries(omakade-sessiond PRIVATE omakade_core)

qt_add_executable(omakade
  src/app/main.cpp
  src/app/CouchNavigationContract.cpp
  src/app/CouchNavigationContract.h
  src/app/CardExport.cpp
  src/app/CardExport.h
  src/app/IdleInhibitor.cpp
  src/app/IdleInhibitor.h
)
if(TARGET Qt6::GuiPrivate)
  # Input fixtures use Qt's platform path so window shortcuts receive real key events.
  target_link_libraries(omakade PRIVATE Qt6::GuiPrivate)
  target_compile_definitions(omakade PRIVATE OMAKADE_PLATFORM_INPUT_TESTS=1)
endif()
if(OMAKADE_IDLE_INHIBIT)
  target_sources(omakade PRIVATE "${OMAKADE_IDLE_INHIBIT_HEADER}" "${OMAKADE_IDLE_INHIBIT_SOURCE}")
  set_source_files_properties("${OMAKADE_IDLE_INHIBIT_SOURCE}" PROPERTIES
    COMPILE_OPTIONS "-w")
  target_include_directories(omakade PRIVATE "${OMAKADE_PROTOCOL_DIR}")
  target_compile_definitions(omakade PRIVATE OMAKADE_IDLE_INHIBIT=1)
  target_link_libraries(omakade PRIVATE PkgConfig::WAYLAND_CLIENT)
endif()

# A pragma Singleton file is only registered as one when CMake says so, and this has to be set
# before the module is declared. Without it the type resolves to nothing and every binding on it
# quietly evaluates to undefined.
set_source_files_properties(qml/components/TextEntry.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/UiMetrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(omakade
  URI Omakade
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/components/TextEntry.qml
    qml/components/UiMetrics.qml
  qml/components/FieldClearButton.qml
  qml/components/ThemedTextField.qml
  qml/components/ThemedTextArea.qml
  qml/components/ThemedComboBox.qml
  qml/components/ThemedCheckBox.qml
  qml/components/ThemedSpinBox.qml
  qml/components/CoverSizeControl.qml
    qml/components/CoverArtwork.qml
    qml/components/YearInReviewCard.qml
    qml/components/SaveProtectionPanel.qml
    qml/components/SaveBackupMenu.qml
    qml/components/GameModePanel.qml
    qml/components/GameStopPanel.qml
    qml/components/LaunchSetupPanel.qml
    qml/components/LibraryRepairPanel.qml
    qml/components/RommSettings.qml
    qml/components/LaunchFeedback.qml
    qml/components/ProtonDbBadge.qml
    qml/components/GameCard.qml
    qml/components/CouchLibraryView.qml
    qml/components/CouchKeyboard.qml
    qml/components/CouchBrowsePanel.qml
    qml/components/GameMetadataEditor.qml
    qml/components/SettingsPanel.qml
    qml/components/AppHeader.qml
    qml/components/UserDates.js
    qml/components/GlassButton.qml
    qml/components/ActionMenu.qml
    qml/components/MenuAction.qml
    qml/components/LibraryView.qml
    qml/screens/ArtworkEditor.qml
    qml/screens/RestoreStartup.qml
    qml/screens/BackupEditor.qml
    qml/screens/SavedFiltersEditor.qml
    qml/screens/BulkOrganizationEditor.qml
    qml/screens/ManualGameEditor.qml
    qml/screens/HomeScreen.qml
    qml/screens/StatsScreen.qml
    qml/screens/YearInReviewPreview.qml
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
  Qt6::DBus
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
install(TARGETS omakade-sessiond omakade-guide-button omakade-guide-guard
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES resources/sessiond-profiles.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/omakade
)
install(FILES packaging/omakade-sessiond.service packaging/omakade-guide-button.service
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user
)
# Start the resident service with the desktop even before the library is opened.
install(CODE "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/systemd/user/graphical-session.target.wants\")
file(CREATE_LINK \"../omakade-sessiond.service\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/systemd/user/graphical-session.target.wants/omakade-sessiond.service\" SYMBOLIC)")
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
install(FILES README.md CONTRIBUTING.md PRIVACY.md SUPPORT.md SECURITY.md RELEASING.md
  CHANGELOG.md docs/COMPATIBILITY.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omakade
)
# The in-game guide shell plugin. Omakade links it into each user's Omarchy plugin folder
# on first run, so package upgrades update it in place.
install(DIRECTORY omarchy-plugin/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/omakade/omarchy-plugin
  PATTERN "__pycache__" EXCLUDE
  PATTERN "README.md" EXCLUDE
  PATTERN "*.py" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
install(DIRECTORY skills/omakade-tv-gaming
  DESTINATION ${CMAKE_INSTALL_DATADIR}/omakade/skills
  FILES_MATCHING PATTERN "*.md"
  PATTERN "*.py" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  PATTERN "__pycache__" EXCLUDE
)

install(FILES docs/GUIDE.md docs/GAME-MODE.md docs/RECORDING-COVERAGE.md
  docs/CONTROLLER-NAVIGATION.md docs/BACKUP-FORMAT.md docs/SAVE-SETS.md docs/COMPATIBILITY.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omakade/docs
)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
