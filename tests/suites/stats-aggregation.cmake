include_guard(DIRECTORY)
# Stats and Home aggregation: interval placement, the legacy estimate, and background builds.
qt_add_executable(omakade_stats_aggregation_tests StatsAggregationTests.cpp)
target_link_libraries(omakade_stats_aggregation_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_stats_aggregation_tests COMMAND omakade_stats_aggregation_tests)
set_tests_properties(omakade_stats_aggregation_tests PROPERTIES TIMEOUT 60
  ENVIRONMENT "TZ=America/New_York;QT_FORCE_STDERR_LOGGING=1")
