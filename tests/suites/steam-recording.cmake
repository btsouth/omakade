# Recording Steam games: the reaper's app id, install folders, the reaper's process tree,
# recovery, and a recorded session linking to its game in Stats.
omakade_test_executable(omakade_steam_recording_tests
  SOURCES SteamRecordingTests.cpp
  ENVIRONMENT TZ=America/New_York
  TIMEOUT 60)
