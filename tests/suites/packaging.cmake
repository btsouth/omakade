# The installed tree enables no service for every account, and the bundled plugin's manifest
# carries the package's version and license.
omakade_python_test(omakade_package_layout
  SCRIPT PackageLayoutTests.py
  ARGS ${CMAKE_COMMAND} ${CMAKE_BINARY_DIR} ${PROJECT_VERSION} ${CMAKE_INSTALL_LIBDIR} ${CMAKE_INSTALL_DATADIR}
  TIMEOUT 120)
