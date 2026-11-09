# Library card status pills stay opaque, readable and clear of the favorite badge.
omakade_test_executable(omakade_status_pill_ui_tests
  SOURCES StatusPillUiTests.cpp
  LIBRARIES Qt6::Quick
  DEFINITIONS OMAKADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  ENVIRONMENT ${OMAKADE_TEST_UI_ENVIRONMENT}
  TIMEOUT 30)
