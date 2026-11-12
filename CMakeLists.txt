cmake_minimum_required(VERSION 3.24)

project(Omakade VERSION 1.19.0 LANGUAGES C CXX)

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
    # The scanner rules are in src/CMakeLists.txt, next to the omakade target that uses them.
  endif()
endif()
message(STATUS "Wayland idle-inhibit: ${OMAKADE_IDLE_INHIBIT}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core DBus Gui Network Qml Quick QuickControls2 Sql Test)
# GuiPrivate provides the Wayland window interface used by the idle inhibitor
# and the real keyboard input path used by the app's self-tests.
find_package(Qt6 6.8 REQUIRED COMPONENTS GuiPrivate)
qt_standard_project_setup(REQUIRES 6.8)

# Before src/, which builds the omakade-uitest harness only when testing is on.
include(CTest)

# Targets and their links are in src/CMakeLists.txt. Each src/<dir>/CMakeLists.txt adds its
# directory's sources by glob, so a new file in an existing directory needs no CMake edit.
add_subdirectory(src)

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
# The units start the installed binaries, so their paths follow the install prefix.
foreach(omakade_unit omakade-sessiond omakade-guide-button)
  configure_file(packaging/${omakade_unit}.service.in packaging/${omakade_unit}.service @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging/${omakade_unit}.service"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user
  )
endforeach()
# No .wants link here: that would start the service for every account on the machine.
# On Omarchy, Omakade enables it for each user the first time they open it.
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
# The documentation keeps the source tree's layout so relative links between pages work;
# PackageLayoutTests checks every one.
install(FILES README.md CONTRIBUTING.md PRIVACY.md SUPPORT.md SECURITY.md RELEASING.md
  CHANGELOG.md COPYRIGHT
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omakade
)
# The in-game guide shell plugin. Omakade links it into each user's Omarchy plugin folder
# on first run, so package upgrades update it in place.
# Its manifest carries the package version.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS omarchy-plugin/manifest.json)
file(READ omarchy-plugin/manifest.json omakade_plugin_manifest)
string(JSON omakade_plugin_manifest SET "${omakade_plugin_manifest}" version "\"${PROJECT_VERSION}\"")
file(CONFIGURE OUTPUT omarchy-plugin/manifest.json CONTENT "${omakade_plugin_manifest}\n" @ONLY)
install(DIRECTORY omarchy-plugin/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/omakade/omarchy-plugin
  PATTERN "__pycache__" EXCLUDE
  PATTERN "README.md" EXCLUDE
  PATTERN "manifest.json" EXCLUDE
  PATTERN "*.py" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/omarchy-plugin/manifest.json"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/omakade/omarchy-plugin
)
# The guides link to the skill, so the documentation carries a copy too.
foreach(omakade_skill_dir omakade/skills doc/omakade/skills)
  install(DIRECTORY skills/omakade-tv-gaming
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${omakade_skill_dir}
    FILES_MATCHING PATTERN "*.md"
    PATTERN "*.py" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    PATTERN "__pycache__" EXCLUDE
  )
endforeach()

install(FILES docs/GUIDE.md docs/GAME-MODE.md docs/GAME-GUIDE.md docs/HIGHLIGHTS.md
  docs/NATIVE-PORTS.md docs/RECORDING-COVERAGE.md docs/CONTROLLER-NAVIGATION.md
  docs/BACKUP-FORMAT.md docs/SAVE-SETS.md docs/COMPATIBILITY.md docs/TROUBLESHOOTING.md
  docs/ARCHITECTURE.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omakade/docs
)
install(FILES docs/assets/library-preview.webp docs/assets/game-details.webp
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omakade/docs/assets
)
install(FILES omarchy-plugin/README.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omakade/omarchy-plugin
)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
