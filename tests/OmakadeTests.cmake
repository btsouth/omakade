# Helpers for registering tests from tests/suites/<name>.cmake. See tests/suites/README.md.
#
# Every helper also takes TIMEOUT <seconds>, RUN_SERIAL, WILL_FAIL and
# ENVIRONMENT <VAR=value>... and sets the ctest property of the same name.

set(OMAKADE_TEST_UI_ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")

# QML runtime errors and warnings that fail any test whose output shows them. The app only logs
# these and keeps running, so without this a broken binding still exits 0. tests/CMakeLists.txt
# adds them to every test's FAIL_REGULAR_EXPRESSION, apart from WILL_FAIL tests, where a match
# would count as the expected failure and hide the problem.
set(OMAKADE_TEST_QML_ERRORS
  "TypeError"
  "ReferenceError"
  "RangeError"
  "Binding loop"
  "polish.*loop"
  "Unable to assign"
  "Cannot assign"
  "is not a type"
  "module .+ is not installed"
  "Could not convert argument"
  "Invalid write to global property")

# omakade_app_test(<name> ARGS <omakade arguments>...)
# Runs the app offscreen with software rendering, as omakade-uitest: the omakade window plus
# the render paths, fixtures and self-tests in src/selftest. ENVIRONMENT adds to that.
function(omakade_app_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "RUN_SERIAL;WILL_FAIL" "TIMEOUT" "ENVIRONMENT;ARGS")
  omakade_check_test_arguments(omakade_app_test ${name})
  add_test(NAME ${name} COMMAND omakade-uitest ${arg_ARGS})
  set(arg_ENVIRONMENT ${OMAKADE_TEST_UI_ENVIRONMENT} ${arg_ENVIRONMENT})
  omakade_set_test_properties(${name})
endfunction()

# omakade_test_executable(<target> SOURCES <files>... [LIBRARIES <targets>...]
#                         [DEFINITIONS <NAME=value>...] [SHARDS <count>])
# Builds a Qt Test binary linked to omakade_core, LIBRARIES and Qt6::Test, and registers it
# as a test of the same name. SHARDS registers <target>_0 to <target>_<count - 1> instead,
# each run with --test-shard=<index>.
function(omakade_test_executable target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "RUN_SERIAL;WILL_FAIL" "TIMEOUT;SHARDS"
    "ENVIRONMENT;SOURCES;LIBRARIES;DEFINITIONS")
  omakade_check_test_arguments(omakade_test_executable ${target})
  qt_add_executable(${target} ${arg_SOURCES})
  target_link_libraries(${target} PRIVATE omakade_core ${arg_LIBRARIES} Qt6::Test)
  if(arg_DEFINITIONS)
    target_compile_definitions(${target} PRIVATE ${arg_DEFINITIONS})
  endif()
  if(arg_SHARDS)
    math(EXPR last_shard "${arg_SHARDS} - 1")
    foreach(shard RANGE 0 ${last_shard})
      add_test(NAME ${target}_${shard} COMMAND ${target} --test-shard=${shard})
      omakade_set_test_properties(${target}_${shard})
    endforeach()
  else()
    add_test(NAME ${target} COMMAND ${target})
    omakade_set_test_properties(${target})
  endif()
endfunction()

# omakade_python_test(<name> SCRIPT <file> [ARGS <arguments>...])
function(omakade_python_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "RUN_SERIAL;WILL_FAIL" "TIMEOUT;SCRIPT" "ENVIRONMENT;ARGS")
  omakade_check_test_arguments(omakade_python_test ${name})
  if(NOT arg_SCRIPT)
    message(FATAL_ERROR "omakade_python_test(${name}) needs SCRIPT.")
  endif()
  get_filename_component(script "${arg_SCRIPT}" ABSOLUTE)
  add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE} ${script} ${arg_ARGS})
  omakade_set_test_properties(${name})
endfunction()

# Fails on arguments the calling helper does not know, so a typo is not silently dropped.
macro(omakade_check_test_arguments helper name)
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${helper}(${name}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
endmacro()

# Applies the shared arguments, as parsed by the calling helper, to one test.
macro(omakade_set_test_properties test_name)
  if(arg_ENVIRONMENT)
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "${arg_ENVIRONMENT}")
  endif()
  if(arg_TIMEOUT)
    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${arg_TIMEOUT})
  endif()
  if(arg_RUN_SERIAL)
    set_tests_properties(${test_name} PROPERTIES RUN_SERIAL TRUE)
  endif()
  if(arg_WILL_FAIL)
    set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
  endif()
endmacro()
