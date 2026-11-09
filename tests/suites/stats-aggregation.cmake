# Stats and Home aggregation: interval placement, the legacy estimate, and background builds.
omakade_test_executable(omakade_stats_aggregation_tests
  SOURCES StatsAggregationTests.cpp
  ENVIRONMENT TZ=America/New_York QT_FORCE_STDERR_LOGGING=1
  TIMEOUT 60)
