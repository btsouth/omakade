# The installed tree enables no service for every account, its units start the binaries from
# the configured prefix, OmaStore's services run binaries the package ships, and the bundled
# plugin's manifest carries the package's version and license. Every relative link in the
# installed docs opens.
omakade_python_test(omakade_package_layout
  SCRIPT PackageLayoutTests.py
  ARGS ${CMAKE_COMMAND} ${CMAKE_BINARY_DIR} ${PROJECT_VERSION} ${CMAKE_INSTALL_LIBDIR} ${CMAKE_INSTALL_DATADIR}
    ${CMAKE_INSTALL_FULL_BINDIR} ${PROJECT_SOURCE_DIR}
  TIMEOUT 120)
