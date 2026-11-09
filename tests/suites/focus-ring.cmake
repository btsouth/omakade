# The focus ring is thick, readable on every surface and distinct from hover and the current card.
omakade_test_executable(omakade_focus_ring_ui_tests
  SOURCES FocusRingUiTests.cpp
  LIBRARIES Qt6::Quick
  DEFINITIONS OMAKADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  ENVIRONMENT ${OMAKADE_TEST_UI_ENVIRONMENT}
  TIMEOUT 30)
