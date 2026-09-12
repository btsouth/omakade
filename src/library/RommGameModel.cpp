#include "library/RommGameModel.h"
#include "app/AppSettings.h"
#include "library/GameRoles.h"
#include "tracking/PlaySessionStore.h"
#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

RommGameModel::RommGameModel(const QString& path, AppSettings* settings, PlaySessionStore* sessions,
                             QObject* parent, QNetworkAccessManager* network,
                             Credentials credentials)
    : QAbstractListModel(parent), m_settings(settings), m_sessions(sessions),
      m_catalog(path, this, network),
      m_credentials(credentials ? std::move(credentials)
                                : [](const QUrl& server, const QByteArray& token, bool store) {
                                    return store ? RommCredentials::store(server, token)
                                                 : RommCredentials::load(server);
                                  }) {
  connect(&m_catalog, &RommCatalog::finished, this, [this](bool success, const QString& message) {
    if (!m_enabled)
      return;
    m_error = success ? QString{} : message;
    m_status = success                                     ? "Connected"
               : message.contains("authentication failed") ? "Unauthorized; cached catalog kept"
                                                           : "Cached catalog; refresh failed";
    reload();
    emit statusChanged();
  });
  connect(settings, &AppSettings::rommConfigurationChanged, this, &RommGameModel::configure);
  connect(settings, &AppSettings::sourcesChanged, this, &RommGameModel::configure);
  if (sessions)
    connect(sessions, &PlaySessionStore::totalsChanged, this, [this] {
      if (!m_games.isEmpty())
        emit dataChanged(index(0), index(m_games.size() - 1),
                         {GameRoles::LastPlayed, GameRoles::Hours, GameRoles::PlaytimeSeconds,
                          GameRoles::PlaytimeText});
    });
  configure();
}
void RommGameModel::configure() {
  const auto server = RommCatalog::serverUrl(QUrl(m_settings->rommUrl()));
  const auto root = m_settings->rommLibraryRoot();
  const bool enabled = m_settings->rommEnabled();
  if (m_server == server && m_root == root && m_enabled == enabled && !m_status.isEmpty())
    return;
  ++m_generation;
  m_credentialQueue->generation.store(m_generation);
  m_enabled = false;
  m_catalog.cancel();
  m_loading = false;
  m_hasToken = false;
  m_server = server;
  m_root = root;
  m_enabled = enabled;
  m_error.clear();
  m_status = !enabled                             ? "Disabled"
             : server.isEmpty() || root.isEmpty() ? "Configure the server and mounted library"
                                                  : "Cached catalog";
  reload();
  emit statusChanged();
  if (enabled && !server.isEmpty() && !root.isEmpty()) {
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
      if (generation == m_generation)
        refresh();
    });
  }
}
void RommGameModel::reload() {
  beginResetModel();
  m_games = m_enabled ? m_catalog.cached(m_server, m_root) : QVector<RommGameRecord>{};
  endResetModel();
}
bool RommGameModel::connectServer(const QString& address, const QString& root,
                                  const QString& token) {
  const auto server = RommCatalog::serverUrl(QUrl(address.trimmed()));
  const QFileInfo folder(root.trimmed());
  if (server.isEmpty() || !folder.isAbsolute() || !folder.isDir() ||
      (!token.isEmpty() && !RommCatalog::validToken(token.toLatin1()))) {
    m_error = "Enter a valid server, existing mounted folder, and Client API Token.";
    emit statusChanged();
    return false;
  }
  m_settings->setRommEnabled(false);
  m_settings->setRommUrl(server.toString());
  m_settings->setRommLibraryRoot(folder.canonicalFilePath());
  if (m_settings->rommUrl() != server.toString() ||
      m_settings->rommLibraryRoot() != folder.canonicalFilePath()) {
    m_error = "Could not save the connection settings.";
    emit statusChanged();
    return false;
  }
  m_settings->setRommEnabled(true);
  if (!token.isEmpty())
    storeToken(token);
  return m_settings->rommEnabled();
}
void RommGameModel::refresh() {
  if (!m_enabled || scanning())
    return;
  credentials({}, false, true);
}
void RommGameModel::storeToken(const QString& token) {
  if (!RommCatalog::validToken(token.toLatin1())) {
    m_error = "Enter a valid RomM Client API Token.";
    emit statusChanged();
    return;
  }
  credentials(token.toLatin1(), true, true);
}
void RommGameModel::disconnectServer(bool forget) {
  m_settings->setRommEnabled(false);
  if (forget)
    credentials({}, true, false);
}
void RommGameModel::credentials(const QByteArray& token, bool store, bool refreshAfter) {
  if (m_server.isEmpty()) {
    m_error = "Set the RomM server address first.";
    emit statusChanged();
    return;
  }
  ++m_generation;
  m_credentialQueue->generation.store(m_generation);
  m_catalog.cancel();
  const auto generation = m_generation;
  m_loading = true;
  m_status = store ? "Updating secure token" : "Reading secure token";
  m_error.clear();
  emit statusChanged();
  auto* watcher = new QFutureWatcher<RommCredentialResult>(this);
  connect(watcher, &QFutureWatcher<RommCredentialResult>::finished, this,
          [this, watcher, generation, token, store, refreshAfter] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation)
              return;
            m_loading = false;
            if (!result.success) {
              m_error = result.error;
              m_status = "Keyring unavailable";
              emit statusChanged();
              return;
            }
            const auto secret = store ? token : result.token;
            m_hasToken = !secret.isEmpty();
            if (refreshAfter && m_enabled && m_hasToken) {
              if (!QFileInfo(m_root).isDir()) {
                m_status = "Mounted library unavailable";
                m_error = "Mount the library, then refresh. Cached entries were kept.";
              } else {
                m_status = "Connecting to RomM";
                m_catalog.refresh(m_server, m_root, secret);
              }
            } else
              m_status = m_enabled ? "Add a Client API Token" : "Disconnected";
            emit statusChanged();
          });
  const auto operation = m_credentials;
  const auto server = m_server;
  const auto queue = m_credentialQueue;
  watcher->setFuture(QtConcurrent::run([operation, server, token, store, queue, generation] {
    std::lock_guard<std::mutex> lock(queue->mutex);
    if (queue->generation.load() != generation)
      return RommCredentialResult{};
    return operation(server, token, store);
  }));
}
bool RommGameModel::available(const QString& path) const {
  const QFileInfo file(path);
  const auto root = QFileInfo(m_root).canonicalFilePath();
  return !root.isEmpty() && file.isFile() && !file.isSymLink() &&
         file.canonicalFilePath().startsWith(root + '/');
}
QStringList RommGameModel::detectedPaths() const {
  return m_root.isEmpty() ? QStringList{} : QStringList{m_root};
}
int RommGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : m_games.size();
}
QHash<int, QByteArray> RommGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles[GameRoles::Installed] = "installed";
  roles[GameRoles::LaunchTarget] = "launchTarget";
  return roles;
}
QVariant RommGameModel::data(const QModelIndex& idx, int role) const {
  if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_games.size())
    return {};
  const auto& game = m_games[idx.row()];
  const auto seconds = PlaySessionStore::displayedSeconds(m_sessions, game.contentPath, 0);
  switch (role) {
  case GameRoles::Title:
    return game.title;
  case GameRoles::Subtitle:
    return game.platform + " · RomM";
  case GameRoles::Description:
    return game.description;
  case GameRoles::Source:
    return "RomM";
  case GameRoles::AppId:
    return QString::fromLatin1(
               QCryptographicHash::hash(m_server.toEncoded(), QCryptographicHash::Sha256).toHex()) +
           ':' + game.appId;
  case GameRoles::InstallPath:
    return game.contentPath;
  case GameRoles::System:
    return game.system;
  case GameRoles::Installed:
    return available(game.contentPath);
  case GameRoles::Hours:
    return seconds / 3600.0;
  case GameRoles::PlaytimeSeconds:
    return seconds;
  case GameRoles::PlaytimeText:
    return GameRoles::formatPlaytime(seconds);
  case GameRoles::PlaytimeProvenance:
    return PlaySessionStore::provenance(m_sessions, game.contentPath, -1);
  case GameRoles::LastPlayed:
    return PlaySessionStore::displayedLastPlayed(m_sessions, game.contentPath, 0);
  case GameRoles::AccentStart:
    return QColor("#667d9c");
  case GameRoles::AccentEnd:
    return QColor("#334259");
  case GameRoles::CoverMark:
    return game.title.left(1);
  case GameRoles::Favorite:
  case GameRoles::Hidden:
  case GameRoles::Flatpak:
  case GameRoles::IsPortal:
    return false;
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
  case GameRoles::Year:
    return 0;
  default:
    return QString{};
  }
}
