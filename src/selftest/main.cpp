#include "app/OmakadeApp.h"
#include "selftest/SelfTest.h"

#include <QtQml/qqmlextensionplugin.h>

// The QML module is a static plugin of omakade_app, shared with the shipped omakade binary.
Q_IMPORT_QML_PLUGIN(OmakadePlugin)

// omakade-uitest is the app the test suite drives: omakade itself, plus the fixtures, render
// paths and self-tests in this folder. It is built with the tests and never installed.
int main(int argc, char* argv[]) {
  SelfTest::Harness harness;
  return runOmakade(argc, argv, &harness);
}
