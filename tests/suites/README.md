# Test suites

Each `<name>.cmake` file here registers one area's tests. `tests/CMakeLists.txt` includes
every file in this folder, in name order, so a new suite needs no edit anywhere else. Give a
new test binary or feature its own suite rather than adding to `tests/CMakeLists.txt`.

Suites are included into the `tests/` directory scope, so:

- relative paths, such as a test's sources or a Python script, are relative to `tests/`;
- `${CMAKE_CURRENT_SOURCE_DIR}` is `tests/` and `${CMAKE_CURRENT_BINARY_DIR}` is the build's
  `tests/` folder, the place for screenshots and other output;
- every test also gets the CI slot assignment at the end of `tests/CMakeLists.txt`;
- every test that is not `WILL_FAIL` fails when its output shows a QML runtime error, such as
  a `TypeError`, `ReferenceError` or binding loop (`OMAKADE_TEST_QML_ERRORS` in
  `tests/OmakadeTests.cmake`), even if it exits 0. Keep `WILL_FAIL` for runs that exit before
  any QML loads; a run that renders and is expected to fail should match its failure message
  with `PASS_REGULAR_EXPRESSION` instead, so the QML checks still apply.

## Helpers

The helpers live in `tests/OmakadeTests.cmake`. Each takes `TIMEOUT <seconds>`,
`RUN_SERIAL`, `WILL_FAIL` and `ENVIRONMENT <VAR=value>...`, which set the ctest property of
the same name.

`omakade_app_test(<name> ARGS <arguments>...)` runs the app offscreen with software rendering
(`OMAKADE_TEST_UI_ENVIRONMENT`), as `omakade-uitest`: the `omakade` window plus the render
paths, fixtures and self-tests in `src/selftest`. `ENVIRONMENT` adds to that.

```cmake
omakade_app_test(omakade_source_rescan_enablement
  TIMEOUT 20
  ARGS --render-overlay=source-rescan --render-size=900x720
       --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/source-rescan.png)
```

`omakade_test_executable(<target> SOURCES <files>...)` builds a Qt Test binary linked to
`omakade_core` and `Qt6::Test` and registers it as a test of the same name. `LIBRARIES` adds
link targets, `DEFINITIONS` adds compile definitions, and `SHARDS <count>` registers
`<target>_0` to `<target>_<count - 1>`, each run with `--test-shard=<index>`.

```cmake
omakade_test_executable(omakade_highlight_tests
  SOURCES HighlightTests.cpp
  LIBRARIES omakade_tracking
  DEFINITIONS OMAKADE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  TIMEOUT 90)
```

`omakade_python_test(<name> SCRIPT <file> [ARGS <arguments>...])` runs a Python script.

Plain `add_test()` and `set_tests_properties()` work too when a test needs something the
helpers do not cover.

## Sources

The same applies to the code under test. Each `src/<dir>/CMakeLists.txt` globs its
directory into a target, `qml/CMakeLists.txt` takes every `.qml` and `.js` file, and
`resources/CMakeLists.txt` bundles `resources/consoles/cards/*.webp` and
`resources/icons/ui/*.svg`. A new file in an existing folder needs a CMake edit only when it
belongs to a different target than the rest of its folder, or is a QML singleton.
