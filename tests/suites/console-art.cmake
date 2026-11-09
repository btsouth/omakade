# The bundled console card art: one decodable card per catalog console, within a size budget.
omakade_test_executable(omakade_console_art_tests
  SOURCES ConsoleArtTests.cpp
  TIMEOUT 30)
omakade_add_console_art(omakade_console_art_tests)
