# QML runtime errors fail every test that should pass, not only the ones that ask for it.
omakade_python_test(omakade_qml_error_policy
  SCRIPT QmlErrorPolicyTests.py
  ARGS ${CMAKE_CTEST_COMMAND} ${CMAKE_BINARY_DIR} ${OMAKADE_TEST_QML_ERRORS}
  TIMEOUT 30)
