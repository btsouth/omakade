# The Omakade QML module. Included from src/CMakeLists.txt, where the omakade target is
# created, rather than added as a subdirectory. Every .qml and .js file under qml/ is part of
# the module, so a new component or screen needs no edit here.

file(GLOB_RECURSE omakade_qml_files CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}/*.qml"
  "${CMAKE_CURRENT_LIST_DIR}/*.js")

# A pragma Singleton file is only registered as one when CMake says so, and this has to be set
# before the module is declared. Without it the type resolves to nothing and every binding on it
# quietly evaluates to undefined. Add new singletons to this list.
set(omakade_qml_singletons
  components/TextEntry.qml
  components/UiMetrics.qml
)

set(omakade_qml_sources)
foreach(qml_file IN LISTS omakade_qml_files)
  set(qml_path "${CMAKE_CURRENT_LIST_DIR}/${qml_file}")
  # Keep the qml/ prefix in the module's resource paths, as when the module was declared from
  # the top of the tree.
  set_source_files_properties("${qml_path}" PROPERTIES QT_RESOURCE_ALIAS "qml/${qml_file}")
  if(qml_file IN_LIST omakade_qml_singletons)
    set_source_files_properties("${qml_path}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
  endif()
  list(APPEND omakade_qml_sources "${qml_path}")
endforeach()
foreach(singleton IN LISTS omakade_qml_singletons)
  if(NOT singleton IN_LIST omakade_qml_files)
    message(FATAL_ERROR "qml/CMakeLists.txt lists the singleton ${singleton}, which does not exist.")
  endif()
endforeach()

qt_add_qml_module(omakade
  URI Omakade
  VERSION 1.0
  # Where the module was built before src/ had its own CMakeLists.txt, next to the binary.
  OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/Omakade"
  QML_FILES ${omakade_qml_sources}
)
