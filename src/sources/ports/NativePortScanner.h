#pragma once

#include "sources/LauncherGame.h"
#include "sources/ports/PortCatalog.h"

#include <QStringList>

// Installed native ports: desktop entries whose command, or the wrapper script it runs, names
// a port from the catalog, and port AppImages in the usual folders.
class NativePortScanner final {
public:
  struct Options {
    QStringList applicationDirectories;
    QStringList appImageDirectories;
  };
  [[nodiscard]] static Options defaultOptions();
  [[nodiscard]] static LauncherScanResult scan(const Options& options, const PortCatalog& catalog);
};
