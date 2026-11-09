# Selected and on/off buttons look selected under focus and report checked to screen readers,
# and game cards announce their state and can be opened from a screen reader.
omakade_test_executable(omakade_selected_state_ui_tests
  SOURCES SelectedStateUiTests.cpp
  LIBRARIES Qt6::Quick
  DEFINITIONS OMAKADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  ENVIRONMENT ${OMAKADE_TEST_UI_ENVIRONMENT}
  TIMEOUT 30)
