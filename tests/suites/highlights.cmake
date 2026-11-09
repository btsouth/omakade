omakade_test_executable(omakade_highlight_tests
  SOURCES HighlightTests.cpp
  LIBRARIES omakade_tracking
  DEFINITIONS OMAKADE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  TIMEOUT 90)

# The guide's clip and a screen recording started from Omarchy never take each other's place.
omakade_python_test(omakade_guide_recorder
  SCRIPT GuideRecorderTests.py
  ARGS ${PROJECT_SOURCE_DIR}/omarchy-plugin/guide-files.py)
