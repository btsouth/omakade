# Resource bundles compiled into the binaries. Included from src/CMakeLists.txt, where the
# targets are created, rather than added as a subdirectory.

# The native port catalog lives in the core library so tests read the same file the app does.
qt_add_resources(omakade_core "omakade_native_ports"
  PREFIX "/ports"
  BASE "${CMAKE_CURRENT_LIST_DIR}"
  FILES
    "${CMAKE_CURRENT_LIST_DIR}/native-ports.json"
)

# The app icon. QML loads it as qrc:/icons/resources/icons/io.github.tsouth89.Omakade.svg.
qt_add_resources(omakade "omakade_icons"
  PREFIX "/icons"
  BASE "${PROJECT_SOURCE_DIR}"
  FILES
    "${CMAKE_CURRENT_LIST_DIR}/icons/io.github.tsouth89.Omakade.svg"
)

# Interface icons: any SVG dropped into resources/icons/ui is bundled automatically and loads
# as qrc:/icons/ui/<name>.svg.
file(GLOB omakade_ui_icons CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/icons/ui/*.svg")
qt_add_resources(omakade "omakade_ui_icons"
  PREFIX "/icons/ui"
  BASE "${CMAKE_CURRENT_LIST_DIR}/icons/ui"
  FILES ${omakade_ui_icons}
)

# Console card art: the WebP cards in resources/consoles/cards, one per ConsoleCatalog id,
# written from the PNG masters by scripts/generate-console-cards.py. The masters are not
# bundled. A function so the console art test bundles exactly the same files.
function(omakade_add_console_art target)
  file(GLOB console_art CONFIGURE_DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/consoles/cards/*.webp")
  qt_add_resources(${target} "omakade_consoles"
    PREFIX "/consoles"
    BASE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/consoles/cards"
    FILES ${console_art}
  )
endfunction()
omakade_add_console_art(omakade)
