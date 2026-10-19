#pragma once

#include "sources/ports/PortCatalog.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class LauncherGameModel;
class UnifiedGameModel;

// Tells the details page about native ports of a game the player has as a dump, so a game
// running in an emulator can point at the port that plays it natively.
class NativePortAdvisor final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int revision READ revision NOTIFY installedPortsChanged)

public:
  NativePortAdvisor(const PortCatalog& catalog, LauncherGameModel* installed,
                    QObject* parent = nullptr);

  // Ports of this installation's game that are not installed, each with its id, name, repo
  // and whether it has a Linux build. Empty for anything that is not an emulator's game.
  [[nodiscard]] int revision() const { return m_revision; }
  Q_INVOKABLE QVariantList portsFor(const QVariantMap& installation) const;
  // Shows an installed port and the dump it plays as one game, with the port as the default.
  // Each pair is linked once, so a pair the player separates stays separate.
  void linkInstalledPorts(UnifiedGameModel* games) const;

signals:
  void installedPortsChanged();

private:
  int m_revision = 0;
  const PortCatalog& m_catalog;
  LauncherGameModel* m_installed = nullptr;
};
