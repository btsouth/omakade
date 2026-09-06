#include "library/ConsolePortalModel.h"

#include "library/ConsoleCatalog.h"
#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QUrl>

#include <algorithm>

namespace {
QColor colorFor(const QString& id, int offset) {
  const QByteArray hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256);
  return QColor::fromHsl((static_cast<unsigned char>(hash.at(offset)) * 359) / 255, 115,
                         offset == 0 ? 105 : 72);
}

// Bundled console art, when the build ships some for this system.
QString coverUrl(const QString& systemId) {
  for (const QString& extension : {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("svg")}) {
    const QString resource = QStringLiteral(":/consoles/%1.%2").arg(systemId, extension);
    if (QFile::exists(resource)) {
      return QStringLiteral("qrc") + resource;
    }
  }
  return {};
}
} // namespace

ConsolePortalModel::ConsolePortalModel(QObject* parent)
    : QAbstractListModel(parent), m_cardSystems(ConsoleCatalog::defaultCardSystems()) {}

void ConsolePortalModel::addRomModel(QAbstractItemModel* model) {
  if (model == nullptr || m_models.contains(model)) {
    return;
  }
  m_models.append(model);
  connect(model, &QAbstractItemModel::modelReset, this, &ConsolePortalModel::rebuild);
  connect(model, &QAbstractItemModel::rowsInserted, this, &ConsolePortalModel::rebuild);
  connect(model, &QAbstractItemModel::rowsRemoved, this, &ConsolePortalModel::rebuild);
  connect(model, &QAbstractItemModel::dataChanged, this,
          [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
            if (roles.isEmpty()) {
              rebuild();
              return;
            }
            for (int role : roles) {
              if (role == GameRoles::Title || role == GameRoles::System ||
                  role == GameRoles::Source || role == GameRoles::Hidden ||
                  role == GameRoles::LastPlayed || role == GameRoles::Hours) {
                rebuild();
                return;
              }
            }
          });
  rebuild();
}

int ConsolePortalModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_portals.size());
}

QVariant ConsolePortalModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_portals.size()) {
    return {};
  }
  return valueForRole(m_portals.at(index.row()), role);
}

QHash<int, QByteArray> ConsolePortalModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  roles.insert(GameRoles::System, "system");
  roles.insert(GameRoles::IsPortal, "isPortal");
  return roles;
}

void ConsolePortalModel::setCardSystems(const QStringList& systems) {
  QStringList normalized;
  for (const QString& system : systems) {
    const QString id = ConsoleCatalog::idFor(system);
    if (!id.isEmpty() && !normalized.contains(id)) {
      normalized.append(id);
    }
  }
  if (m_cardSystems == normalized) {
    return;
  }
  m_cardSystems = normalized;
  rebuild();
}

int ConsolePortalModel::gameCount(const QString& systemId) const {
  for (const Portal& portal : m_portals) {
    if (portal.systemId == systemId) {
      return portal.gameCount;
    }
  }
  return 0;
}

void ConsolePortalModel::rebuild() {
  QHash<QString, Portal> grouped;
  for (QAbstractItemModel* model : m_models) {
    for (int row = 0; row < model->rowCount(); ++row) {
      const QModelIndex index = model->index(row, 0);
      const QString rawSystem = index.data(GameRoles::System).toString();
      if (rawSystem.isEmpty() || index.data(GameRoles::IsPortal).toBool()) {
        continue;
      }
      const QString systemId = ConsoleCatalog::idFor(rawSystem);
      if (!m_cardSystems.contains(systemId)) {
        continue;
      }
      Portal& portal = grouped[systemId];
      const QString source = index.data(GameRoles::Source).toString();
      if (!source.isEmpty() && !portal.sources.contains(source)) {
        portal.sources.append(source);
      }
      if (portal.systemId.isEmpty()) {
        portal.systemId = systemId;
        portal.title = ConsoleCatalog::displayNameFor(rawSystem);
        portal.accentStart = colorFor(systemId, 0);
        portal.accentEnd = colorFor(systemId, 1);
      }
      portal.gameCount += 1;
      portal.lastPlayed = std::max(portal.lastPlayed, index.data(GameRoles::LastPlayed).toLongLong());
      portal.hours = std::max(portal.hours, index.data(GameRoles::Hours).toInt());
    }
  }
  QVector<Portal> portals = grouped.values();
  std::sort(portals.begin(), portals.end(), [](const Portal& left, const Portal& right) {
    return left.title.localeAwareCompare(right.title) < 0;
  });
  // A reset here cascades into a full rebuild of the unified library and a
  // re-filter of every row. Playtime or count changes on an existing portal
  // only need a data change; reset when a console appears or disappears.
  bool sameConsoles = portals.size() == m_portals.size();
  for (int index = 0; sameConsoles && index < portals.size(); ++index) {
    sameConsoles = portals.at(index).systemId == m_portals.at(index).systemId;
  }
  if (sameConsoles) {
    m_portals = portals;
    if (!m_portals.isEmpty()) {
      emit dataChanged(index(0), index(m_portals.size() - 1));
    }
    return;
  }
  beginResetModel();
  m_portals = portals;
  endResetModel();
}

QVariant ConsolePortalModel::valueForRole(const Portal& portal, int role) const {
  switch (role) {
  case GameRoles::Title:
    return portal.title;
  case GameRoles::Subtitle:
    return portal.gameCount == 1 ? QStringLiteral("1 game")
                                 : QStringLiteral("%1 games").arg(portal.gameCount);
  case GameRoles::Description:
    return QStringLiteral("Open this console to browse its games.");
  case GameRoles::Hours:
    return portal.hours;
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
  case GameRoles::Year:
    return 0;
  case GameRoles::Favorite:
  case GameRoles::Hidden:
  case GameRoles::Flatpak:
    return false;
  case GameRoles::Recent:
    return portal.lastPlayed > 0;
  case GameRoles::LastPlayed:
    return portal.lastPlayed;
  case GameRoles::AccentStart:
    return portal.accentStart;
  case GameRoles::AccentEnd:
    return portal.accentEnd;
  case GameRoles::CoverMark:
    return portal.title.left(1).toUpper();
  case GameRoles::AppId:
    return QStringLiteral("portal:%1").arg(portal.systemId);
  case GameRoles::CoverPath:
    return coverUrl(portal.systemId);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
  case GameRoles::InstallPath:
  case GameRoles::Runner:
  case GameRoles::LaunchTarget:
    return QString{};
  case GameRoles::Source:
    return portal.sources.isEmpty() ? QStringLiteral("RetroArch") : portal.sources.constFirst();
  case GameRoles::LinkedSources:
    return portal.sources.join(QStringLiteral(" + "));
  case GameRoles::System:
    return portal.systemId;
  case GameRoles::IsPortal:
    return true;
  default:
    return {};
  }
}
