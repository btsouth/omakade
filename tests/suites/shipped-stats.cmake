# Stats as the shipped omakade opens it, without the context properties omakade-uitest's
# fixtures set: the screen loads with no QML error and runs a card preview asked for earlier.
omakade_test_executable(omakade_shipped_stats_tests
  SOURCES ShippedStatsTests.cpp
  LIBRARIES omakade_app omakade_appplugin
  ENVIRONMENT ${OMAKADE_TEST_UI_ENVIRONMENT}
  TIMEOUT 30)
