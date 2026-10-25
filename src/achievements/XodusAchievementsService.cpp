#include "achievements/XodusAchievementsService.h"

#include "sources/xodus/XodusScanner.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QSqlQuery>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr qint64 kMaximumResponseBytes = 4 * 1024 * 1024;
constexpr qint64 kAchievementRefreshSeconds = 15 * 60;
constexpr int kTimeoutMs = 35000;
constexpr int kMaximumAchievements = 500;

// Achievement art is a full screenshot several megabytes in size; the image service scales it.
QString iconUrl(const QString& value) {
  QUrl url(value);
  const QString host = url.host().toLower();
  if (url.scheme() != QStringLiteral("https") || !url.userInfo().isEmpty() || url.port() != -1 ||
      (host != QStringLiteral("images-eds-ssl.xboxlive.com") &&
       host != QStringLiteral("images-eds.xboxlive.com"))) {
    return {};
  }
  QUrlQuery query(url);
  query.removeAllQueryItems(QStringLiteral("w"));
  query.removeAllQueryItems(QStringLiteral("h"));
  query.removeAllQueryItems(QStringLiteral("format"));
  query.addQueryItem(QStringLiteral("w"), QStringLiteral("192"));
  query.addQueryItem(QStringLiteral("h"), QStringLiteral("108"));
  query.addQueryItem(QStringLiteral("format"), QStringLiteral("jpg"));
  url.setQuery(query);
  return url.toString(QUrl::FullyEncoded);
}
} // namespace

XodusAchievementsService::XodusAchievementsService(const QString& databasePath, QObject* parent)
    : QObject(parent),
      m_connectionName(
          QStringLiteral("omakade-xodus-achievements-%1").arg(reinterpret_cast<quintptr>(this))) {
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(databasePath);
  m_database.open();
}

XodusAchievementsService::~XodusAchievementsService() {
  if (m_process != nullptr) {
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(1000);
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

QString XodusAchievementsService::cacheKey(const QString& gameId) {
  return QStringLiteral("xodus:") + gameId;
}

QString XodusAchievementsService::profileRoot() const {
  if (!m_profileRoot.isEmpty()) return m_profileRoot;
  const QStringList roots = XodusScanner::discoverRoots();
  return roots.isEmpty() ? QString{} : roots.first();
}

bool XodusAchievementsService::hasApiKey() const {
  return !XodusScanner::backendCli(profileRoot()).isEmpty();
}

XodusAchievementsResult XodusAchievementsService::parse(const QByteArray& json,
                                                        const QString& titleId) {
  XodusAchievementsResult result;
  if (json.size() > kMaximumResponseBytes) return result;
  const QJsonObject root = QJsonDocument::fromJson(json).object();
  const QJsonArray items = root.value(QStringLiteral("items")).toArray();
  if (root.value(QStringLiteral("schema")).toInt() != 1 ||
      root.value(QStringLiteral("titleId")).toString() != titleId ||
      !root.value(QStringLiteral("items")).isArray() || items.size() > kMaximumAchievements) {
    return result;
  }
  QSet<QString> ids;
  for (const QJsonValue& value : items) {
    const QJsonObject item = value.toObject();
    const QString id = item.value(QStringLiteral("id")).toString();
    const QString name = item.value(QStringLiteral("name")).toString();
    const QString state = item.value(QStringLiteral("state")).toString();
    if (id.isEmpty() || id.size() > 180 || ids.contains(id) || name.isEmpty() ||
        (state != QStringLiteral("unlocked") && state != QStringLiteral("in-progress") &&
         state != QStringLiteral("locked"))) {
      return {};
    }
    ids.insert(id);
    XodusAchievementRecord record{
        .apiName = id,
        .title = name.left(256),
        .description = item.value(QStringLiteral("description")).toString().left(1500),
        .iconUrl = iconUrl(item.value(QStringLiteral("iconUrl")).toString()),
        .unlocked = state == QStringLiteral("unlocked"),
        .hidden = item.value(QStringLiteral("secret")).toBool(),
        .progress = qBound(0.0, item.value(QStringLiteral("progress")).toDouble(), 100.0)};
    if (record.unlocked) {
      ++result.unlocked;
      const QDateTime when = QDateTime::fromString(
          item.value(QStringLiteral("unlockedAt")).toString(), Qt::ISODateWithMs);
      record.unlockTime = when.isValid() ? when.toSecsSinceEpoch() : 0;
    }
    result.achievements.append(record);
  }
  result.valid = true;
  return result;
}

QString XodusAchievementsService::installPath(const QString& gameId) const {
  if (!m_database.isOpen()) return {};
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("SELECT install_path FROM xodus_games WHERE game_id = ?"));
  query.addBindValue(gameId);
  return query.exec() && query.next() ? query.value(0).toString() : QString{};
}

void XodusAchievementsService::refreshAchievementsIfStale(const QString& gameId) {
  if (gameId.isEmpty()) return;
  if (busy()) {
    m_pendingGameId = gameId;
    return;
  }
  if (!m_database.isOpen()) return;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT updated_at FROM achievement_summary WHERE app_id = ? AND source = 'xbox'"));
  query.addBindValue(cacheKey(gameId));
  if (query.exec() && query.next() &&
      QDateTime::currentSecsSinceEpoch() - query.value(0).toLongLong() <
          kAchievementRefreshSeconds) {
    return;
  }
  refreshAchievements(gameId);
}

void XodusAchievementsService::refreshAchievements(const QString& gameId) {
  if (gameId.isEmpty() || busy()) return;
  const QString cli = XodusScanner::backendCli(profileRoot());
  if (cli.isEmpty()) {
    setStatus(QStringLiteral("unsupported"),
              QStringLiteral("Open Xodus Nexus and sign in to see Xbox achievements."));
    emit accountChanged();
    return;
  }
  const QString titleId = XodusScanner::xboxTitleId(installPath(gameId));
  if (titleId.isEmpty()) {
    setStatus(QStringLiteral("unsupported"),
              QStringLiteral("This game does not say which Xbox title it is."));
    return;
  }
  m_gameId = gameId;
  m_titleId = titleId;
  m_process = new QProcess(this);
  m_process->setProgram(cli);
  m_process->setArguments({QStringLiteral("achievements"), titleId, QStringLiteral("--json")});
  m_process->setStandardInputFile(QProcess::nullDevice());
  m_process->setStandardErrorFile(QProcess::nullDevice());
  m_timeout = new QTimer(m_process);
  m_timeout->setSingleShot(true);
  connect(m_timeout, &QTimer::timeout, m_process, &QProcess::kill);
  connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
    if (m_process->bytesAvailable() > kMaximumResponseBytes) m_process->kill();
  });
  connect(m_process, &QProcess::finished, this, &XodusAchievementsService::finish);
  connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) finish();
  });
  m_process->start();
  m_timeout->start(kTimeoutMs);
  emit busyChanged();
  setStatus(QStringLiteral("refreshing"), QStringLiteral("Reading Xbox achievements"));
}

void XodusAchievementsService::finish() {
  if (m_process == nullptr) return;
  QProcess* process = m_process;
  m_process = nullptr;
  const bool exited = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0 &&
                      process->error() != QProcess::FailedToStart;
  const QByteArray output =
      exited ? process->read(kMaximumResponseBytes + 1) : QByteArray{};
  process->disconnect(this);
  process->deleteLater();
  emit busyChanged();

  const XodusAchievementsResult result = parse(output, m_titleId);
  if (!exited || !result.valid) {
    setStatus(QStringLiteral("error"),
              QStringLiteral("Xodus Nexus could not read Xbox achievements. Check that you are "
                             "signed in and online."));
  } else if (!persist(m_gameId, result)) {
    setStatus(QStringLiteral("error"), QStringLiteral("Could not cache Xbox achievements"));
  } else {
    setStatus(QStringLiteral("connected"),
              QStringLiteral("Updated %1 Xbox achievements").arg(result.achievements.size()));
    emit achievementsUpdated(cacheKey(m_gameId));
  }
  if (!m_pendingGameId.isEmpty()) {
    const QString next = m_pendingGameId;
    m_pendingGameId.clear();
    QTimer::singleShot(0, this, [this, next] { refreshAchievementsIfStale(next); });
  }
}

bool XodusAchievementsService::persist(const QString& gameId,
                                       const XodusAchievementsResult& result) {
  if (!m_database.isOpen() || !m_database.transaction()) return false;
  const QString key = cacheKey(gameId);
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO achievement_summary(app_id, unlocked, total, source, updated_at) VALUES(?, ?, "
      "?, 'xbox', ?) ON CONFLICT(app_id) DO UPDATE SET unlocked = excluded.unlocked, total = "
      "excluded.total, source = excluded.source, updated_at = excluded.updated_at"));
  query.addBindValue(key);
  query.addBindValue(result.unlocked);
  query.addBindValue(static_cast<int>(result.achievements.size()));
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  bool okay = query.exec();
  query.prepare(QStringLiteral("DELETE FROM achievements WHERE app_id = ?"));
  query.addBindValue(key);
  okay = okay && query.exec();
  for (const XodusAchievementRecord& achievement : result.achievements) {
    query.prepare(QStringLiteral(
        "INSERT INTO achievements(app_id, api_name, title, description, icon_url, icon_path, "
        "unlocked, unlock_time, rarity, hidden, current_progress, maximum_progress, source) "
        "VALUES(?, ?, ?, ?, ?, '', ?, ?, 0, ?, ?, ?, 'xbox')"));
    query.addBindValue(key);
    query.addBindValue(achievement.apiName);
    query.addBindValue(achievement.title);
    query.addBindValue(achievement.description);
    query.addBindValue(achievement.iconUrl);
    query.addBindValue(achievement.unlocked);
    query.addBindValue(achievement.unlockTime);
    query.addBindValue(achievement.hidden);
    const bool partway = !achievement.unlocked && achievement.progress > 0;
    query.addBindValue(partway ? achievement.progress : 0.0);
    query.addBindValue(partway ? 100.0 : 0.0);
    okay = okay && query.exec();
  }
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    return false;
  }
  return true;
}

void XodusAchievementsService::setStatus(const QString& state, const QString& text) {
  m_state = state;
  m_statusText = text;
  emit statusChanged();
}
