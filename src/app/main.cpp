#include "app/OmakadeApp.h"

#include <QtQml/qqmlextensionplugin.h>

// The QML module is a static plugin of omakade_app, shared with omakade-uitest.
Q_IMPORT_QML_PLUGIN(OmakadePlugin)

int main(int argc, char* argv[]) {
  return runOmakade(argc, argv, nullptr);
}
