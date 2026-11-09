# An empty library offers to set up sources, rescan and read the guide, and keeps its text readable.
omakade_test_executable(omakade_empty_library_ui_tests
  SOURCES EmptyLibraryUiTests.cpp
  LIBRARIES Qt6::Quick
  DEFINITIONS OMAKADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  ENVIRONMENT ${OMAKADE_TEST_UI_ENVIRONMENT}
  TIMEOUT 30)
