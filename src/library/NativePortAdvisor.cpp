#include "library/NativePortAdvisor.h"

#include "launch/GameLauncher.h"
#include "library/ConsoleCatalog.h"
#include "library/GameRoles.h"
#include "library/LauncherGameModel.h"
#include "library/UnifiedGameModel.h"

#include <algorithm>
#include <QSet>

NativePortAdvisor::NativePortAdvisor(const PortCatalog& catalog, LauncherGameModel* installed,
                                     QObject* parent)
    : QObject(parent), m_catalog(catalog), m_installed(installed) {
  if (m_installed != nullptr) {
    connect(m_installed, &QAbstractItemModel::modelReset, this, [this] {
      ++m_revision;
      emit installedPortsChanged();
    });
  }
}

namespace {
bool isDumpSource(const QString& source) {
  return GameLauncher::isEmulatorSourceName(source) || source == QLatin1String("Xenia") ||
         source == QLatin1String("Kyty");
}
} // namespace

void NativePortAdvisor::linkInstalledPorts(UnifiedGameModel* games) const {
  if (games == nullptr || m_installed == nullptr) {
    return;
  }
  const QStringList installedIds = m_installed->gameIds();
  if (installedIds.isEmpty()) {
    return;
  }
  const QSet<QString> installed(installedIds.cbegin(), installedIds.cend());
  QSet<QString> systems;
  for (const NativePort& port : m_catalog.ports()) {
    if (installed.contains(port.id)) {
      systems.insert(port.system);
    }
  }
  if (systems.isEmpty()) {
    return;
  }
  struct Pair {
    QString portId;
    QString source;
    QString runner;
    QString appId;
  };
  QVector<Pair> pairs;
  for (int row = 0; row < games->rowCount(); ++row) {
    const QModelIndex card = games->index(row, 0);
    const QStringList sources = card.data(GameRoles::LinkedSources).toString().split(QStringLiteral(" + "));
    if (!isDumpSource(card.data(GameRoles::Source).toString()) &&
        std::none_of(sources.cbegin(), sources.cend(), isDumpSource)) {
      continue;
    }
    // Include dumps inside an existing group, even when its card represents a port.
    for (const QVariant& value : games->installationIdentities(row)) {
      const QVariantMap dump = value.toMap();
      const QString source = dump.value(QStringLiteral("source")).toString();
      if (!isDumpSource(source) ||
          !systems.contains(ConsoleCatalog::idFor(dump.value(QStringLiteral("system")).toString()))) {
        continue;
      }
      for (const NativePort* port : m_catalog.portsFor(
               ConsoleCatalog::idFor(dump.value(QStringLiteral("system")).toString()),
               dump.value(QStringLiteral("appId")).toString(),
               dump.value(QStringLiteral("title")).toString())) {
        if (installed.contains(port->id)) {
          pairs.append({port->id, source, dump.value(QStringLiteral("runner")).toString(),
                        dump.value(QStringLiteral("appId")).toString()});
        }
      }
    }
  }

  for (const Pair& pair : pairs) {
    games->linkOnce(QStringLiteral("Ports"), {}, pair.portId, pair.source, pair.runner, pair.appId);
  }
}

QVariantList NativePortAdvisor::portsFor(const QVariantMap& installation) const {
  const QString source = installation.value(QStringLiteral("source")).toString();
  if (!isDumpSource(source)) {
    return {};
  }
  const QStringList installed = m_installed != nullptr ? m_installed->gameIds() : QStringList{};
  QVariantList ports;
  for (const NativePort* port :
       m_catalog.portsFor(ConsoleCatalog::idFor(installation.value(QStringLiteral("system")).toString()),
                          installation.value(QStringLiteral("appId")).toString(),
                          installation.value(QStringLiteral("title")).toString())) {
    if (installed.contains(port->id)) {
      continue;
    }
    ports.append(QVariantMap{{QStringLiteral("id"), port->id},
                             {QStringLiteral("name"), port->name},
                             {QStringLiteral("repo"), port->repo},
                             {QStringLiteral("linux"), port->linux}});
  }
  return ports;
}
