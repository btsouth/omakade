#include "highlights/SteamAchievements.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace {
constexpr int kPollMs = 2000;
} // namespace

SteamAchievements::SteamAchievements(QStringList statsDirectories, QObject* parent)
    : QObject(parent), m_directories(std::move(statsDirectories)) {
  m_poll.setInterval(kPollMs);
  connect(&m_poll, &QTimer::timeout, this, &SteamAchievements::check);
}

QStringList SteamAchievements::defaultDirectories() {
  const QString home = QDir::homePath();
  QStringList directories;
  for (const QString& root : {home + QStringLiteral("/.local/share/Steam"), home + QStringLiteral("/.steam/steam"),
                              home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam")}) {
    const QString stats = QFileInfo(root + QStringLiteral("/appcache/stats")).canonicalFilePath();
    // ~/.steam/steam usually links to the native install; read each real folder once.
    if (!stats.isEmpty() && !directories.contains(stats)) directories.append(stats);
  }
  return directories;
}

QHash<QString, qint64> SteamAchievements::unlocks(const ValveKeyValues& stats) {
  QHash<QString, qint64> found;
  const ValveKeyValues* cache = stats.object(QStringLiteral("cache"));
  if (cache == nullptr) return found;
  for (auto stat = cache->objects.cbegin(); stat != cache->objects.cend(); ++stat) {
    const ValveKeyValues* times = stat.value().object(QStringLiteral("AchievementTimes"));
    if (times == nullptr) continue;
    for (auto bit = times->values.cbegin(); bit != times->values.cend(); ++bit) {
      bool ok = false;
      const qint64 time = bit.value().toLongLong(&ok);
      if (ok && time > 0) found.insert(stat.key() + QLatin1Char('/') + bit.key(), time);
    }
  }
  return found;
}

QString SteamAchievements::displayName(const ValveKeyValues& schema, const QString& appId,
                                       const QString& unlockKey, const QString& language) {
  const QString stat = unlockKey.section(QLatin1Char('/'), 0, 0);
  const QString bit = unlockKey.section(QLatin1Char('/'), 1, 1);
  const ValveKeyValues* game = schema.object(appId);
  const ValveKeyValues* stats = game != nullptr ? game->object(QStringLiteral("stats")) : nullptr;
  const ValveKeyValues* statObject = stats != nullptr ? stats->object(stat) : nullptr;
  const ValveKeyValues* bits = statObject != nullptr ? statObject->object(QStringLiteral("bits")) : nullptr;
  const ValveKeyValues* entry = bits != nullptr ? bits->object(bit) : nullptr;
  if (entry == nullptr) return {};
  const ValveKeyValues* display = entry->object(QStringLiteral("display"));
  const ValveKeyValues* names = display != nullptr ? display->object(QStringLiteral("name")) : nullptr;
  if (names != nullptr) {
    for (const QString& choice : {language, QStringLiteral("english")}) {
      const QString name = names->value(choice).trimmed();
      if (!name.isEmpty()) return name;
    }
  }
  return entry->value(QStringLiteral("name")).trimmed();
}

QString SteamAchievements::steamLanguage() {
  switch (QLocale::system().language()) {
  case QLocale::German: return QStringLiteral("german");
  case QLocale::French: return QStringLiteral("french");
  case QLocale::Italian: return QStringLiteral("italian");
  case QLocale::Spanish: return QStringLiteral("spanish");
  case QLocale::Portuguese:
    return QLocale::system().territory() == QLocale::Brazil ? QStringLiteral("brazilian") : QStringLiteral("portuguese");
  case QLocale::Russian: return QStringLiteral("russian");
  case QLocale::Polish: return QStringLiteral("polish");
  case QLocale::Japanese: return QStringLiteral("japanese");
  case QLocale::Korean: return QStringLiteral("koreana");
  case QLocale::Chinese:
    return QLocale::system().script() == QLocale::TraditionalChineseScript ? QStringLiteral("tchinese")
                                                                           : QStringLiteral("schinese");
  default: return QStringLiteral("english");
  }
}

QStringList SteamAchievements::files() const {
  QStringList found;
  if (m_appId.isEmpty()) return found;
  const QString pattern = QStringLiteral("UserGameStats_*_%1.bin").arg(m_appId);
  for (const QString& directory : m_directories)
    for (const QString& name : QDir(directory).entryList({pattern}, QDir::Files))
      found.append(directory + QLatin1Char('/') + name);
  return found;
}

void SteamAchievements::watch(const QString& appId) {
  static const QRegularExpression digits(QStringLiteral("^[0-9]{1,10}$"));
  const QString id = digits.match(appId).hasMatch() ? appId : QString();
  if (id == m_appId) return;
  m_appId = id;
  m_modified.clear();
  m_known.clear();
  if (m_appId.isEmpty()) {
    m_poll.stop();
    return;
  }
  // Everything already unlocked is the baseline: only what comes after is news.
  for (const QString& file : files()) {
    ValveKeyValues stats;
    m_modified.insert(file, QFileInfo(file).lastModified());
    if (ValveKeyValuesParser::parseBinaryFile(file, &stats)) m_known.insert(unlocks(stats));
  }
  m_poll.start();
}

void SteamAchievements::check() {
  if (m_appId.isEmpty()) return;
  QHash<QString, qint64> fresh;
  for (const QString& file : files()) {
    const QDateTime modified = QFileInfo(file).lastModified();
    const auto seen = m_modified.constFind(file);
    if (seen != m_modified.cend() && seen.value() == modified) continue;
    ValveKeyValues stats;
    // A file caught mid-write fails to parse; it is read again on the next poll.
    if (!ValveKeyValuesParser::parseBinaryFile(file, &stats)) continue;
    m_modified.insert(file, modified);
    const QHash<QString, qint64> current = unlocks(stats);
    for (auto unlock = current.cbegin(); unlock != current.cend(); ++unlock) {
      if (m_known.contains(unlock.key())) continue;
      m_known.insert(unlock.key(), unlock.value());
      if (now() - unlock.value() <= freshSeconds && unlock.value() <= now() + 5) fresh.insert(unlock.key(), unlock.value());
    }
  }
  if (fresh.isEmpty()) return;
  ValveKeyValues schema;
  bool haveSchema = false;
  for (const QString& directory : m_directories) {
    const QString path = directory + QStringLiteral("/UserGameStatsSchema_%1.bin").arg(m_appId);
    if (QFileInfo::exists(path) && ValveKeyValuesParser::parseBinaryFile(path, &schema)) { haveSchema = true; break; }
  }
  const QString language = steamLanguage();
  // In the order they happened, so a shared clip names them in that order too.
  QList<QPair<qint64, QString>> ordered;
  for (auto unlock = fresh.cbegin(); unlock != fresh.cend(); ++unlock) ordered.append({unlock.value(), unlock.key()});
  std::sort(ordered.begin(), ordered.end());
  for (const auto& [time, key] : ordered) {
    const QString name = haveSchema ? displayName(schema, m_appId, key, language) : QString();
    emit unlocked(name.isEmpty() ? QStringLiteral("Achievement unlocked") : name, time);
  }
}
