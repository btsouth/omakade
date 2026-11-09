# The targets and what they link. Sources come from each directory's CMakeLists.txt, which
# globs its .cpp and .h files into the target that directory belongs to and names the few
# files that belong to another target.

# Adds every .cpp and .h in the calling directory to <target>. RECURSE takes subdirectories
# too. Each TO <other> group names files that build into <other> instead. A file can be in
# more than one TO group, and each one must exist, so a file can't be left out of every target.
#
#   omakade_add_directory_sources(<target> [RECURSE] [TO <other> <file>...]...)
function(omakade_add_directory_sources target)
  set(glob GLOB)
  set(destinations "")
  set(destination "")
  set(expect_destination FALSE)
  foreach(arg IN LISTS ARGN)
    if(expect_destination)
      if(NOT TARGET "${arg}")
        message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE} sends files to ${arg}, which is not a target.")
      endif()
      set(destination "${arg}")
      list(APPEND destinations "${arg}")
      set(expect_destination FALSE)
    elseif(arg STREQUAL "RECURSE")
      set(glob GLOB_RECURSE)
    elseif(arg STREQUAL "TO")
      set(expect_destination TRUE)
    elseif(destination STREQUAL "")
      message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE} names ${arg} before any TO <target>.")
    else()
      list(APPEND moved_to_${destination} "${arg}")
      list(APPEND moved "${arg}")
    endif()
  endforeach()
  if(expect_destination)
    message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE} ends with TO and no target.")
  endif()

  file(${glob} sources CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
  foreach(file IN LISTS moved)
    if(NOT file IN_LIST sources)
      message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE} names ${file}, which does not exist.")
    endif()
  endforeach()
  if(moved)
    list(REMOVE_ITEM sources ${moved})
  endif()
  target_sources(${target} PRIVATE ${sources})

  list(REMOVE_DUPLICATES destinations)
  foreach(other IN LISTS destinations)
    if(NOT moved_to_${other})
      message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE} sends no files to ${other}.")
    endif()
    target_sources(${other} PRIVATE ${moved_to_${other}})
  endforeach()
endfunction()

# The binaries find each other next to themselves, so they all build into the top of the
# build directory, as ./build/<preset>/omakade.
set(omakade_executables omakade omakade-sessiond omakade-guide-button omakade-guide-guard)

# Play session tracking, shared by the Omakade window and the omakade-sessiond
# recorder. Kept separate so the daemon links only Qt Core, Sql, and Network.
add_library(omakade_tracking STATIC)

target_include_directories(omakade_tracking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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

qt_add_executable(omakade-sessiond)
target_link_libraries(omakade-sessiond PRIVATE omakade_tracking)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade-sessiond PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The controller Guide button listener. Qt Core only, and read-only access to evdev.
add_library(omakade_guide STATIC)
target_include_directories(omakade_guide PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(omakade_guide PUBLIC Qt6::Core)

qt_add_executable(omakade-guide-button)
target_link_libraries(omakade-guide-button PRIVATE omakade_guide Qt6::Network)
target_compile_definitions(omakade-guide-button PRIVATE OMAKADE_VERSION="${PROJECT_VERSION}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade_guide PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(omakade-guide-button PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(omakade_guide_runtime STATIC)
target_include_directories(omakade_guide_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(omakade_guide_runtime PUBLIC omakade_guide omakade_tracking Qt6::Gui Qt6::Network Qt6::Concurrent)
qt_add_executable(omakade-guide-guard)
target_link_libraries(omakade-guide-guard PRIVATE omakade_guide_runtime)

add_library(omakade_core STATIC)

target_include_directories(omakade_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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

qt_add_executable(omakade)
target_link_libraries(omakade PRIVATE Qt6::GuiPrivate)
if(OMAKADE_IDLE_INHIBIT)
  # Custom command outputs only reach targets created in the same directory, so the
  # scanner rules live here rather than next to the protocol lookup in the top-level file.
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
  target_sources(omakade PRIVATE "${OMAKADE_IDLE_INHIBIT_HEADER}" "${OMAKADE_IDLE_INHIBIT_SOURCE}")
  set_source_files_properties("${OMAKADE_IDLE_INHIBIT_SOURCE}" PROPERTIES
    COMPILE_OPTIONS "-w")
  target_include_directories(omakade PRIVATE "${OMAKADE_PROTOCOL_DIR}")
  target_compile_definitions(omakade PRIVATE OMAKADE_IDLE_INHIBIT=1)
  target_link_libraries(omakade PRIVATE PkgConfig::WAYLAND_CLIENT)
endif()

# Qt builds a QML module or resource bundle correctly only from the directory scope that
# created its target, so these two files are included here rather than added as
# subdirectories.
include(${PROJECT_SOURCE_DIR}/qml/CMakeLists.txt)
include(${PROJECT_SOURCE_DIR}/resources/CMakeLists.txt)

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

set_target_properties(${omakade_executables} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

add_subdirectory(achievements)
add_subdirectory(app)
add_subdirectory(artwork)
add_subdirectory(backup)
add_subdirectory(gamemode)
add_subdirectory(guide)
add_subdirectory(guidebutton)
add_subdirectory(highlights)
add_subdirectory(input)
add_subdirectory(launch)
add_subdirectory(library)
add_subdirectory(metadata)
add_subdirectory(saves)
add_subdirectory(sessiond)
add_subdirectory(sources)
add_subdirectory(streaming)
add_subdirectory(theme)
add_subdirectory(tracking)
