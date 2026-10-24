#include "tracking/SessionTitleIndex.h"

#include <QSet>
#include <QCryptographicHash>
#include <QtEndian>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QtGlobal>

namespace {
// Emulator caches that carry both a display name and a content path. Each entry
// is the table, the columns holding them, and the Omakade source the cache
// belongs to. A cache whose columns differ is skipped instead of guessed at.
//
// identityColumn is the per-cache identity an attribution adapter resolves through. Every cache
// names it game_id, but what it holds depends on the source: for Dolphin it is the emulator's own
// disc id, which is exactly what its playtime record is keyed by, while other caches hold a
// content-derived value or a "path:" pseudo-id for games the source could not identify. An adapter
// has to be built against the source it serves instead of trusting this column blindly.
struct CacheSpec {
  const char* table;
  const char* titleColumn;
  const char* pathColumn;
  const char* source;
  const char* identityColumn;
};

constexpr CacheSpec kCaches[] = {
    {"retroarch_games", "name", "content_path", "RetroArch", "game_id"},
    {"pcsx2_games", "name", "path", "PCSX2", "game_id"},
    {"rpcs3_games", "name", "path", "RPCS3", "game_id"},
    {"ppsspp_games", "name", "path", "PPSSPP", "game_id"},
    {"ryujinx_games", "name", "path", "Ryujinx", "game_id"},
    {"dolphin_games", "name", "path", "Dolphin", "game_id"},
    {"cemu_games", "name", "path", "Cemu", "game_id"},
    {"melonds_games", "name", "path", "melonDS", "game_id"},
    {"shadps4_games", "name", "path", "shadPS4", "game_id"},
    {"xenia_games", "name", "path", "Xenia", "game_id"},
};

// Emulator profile names whose titles live in another source's cache. A profile
// can name binaries Omakade has no source for: the yuzu-derived Switch emulators
// share Ryujinx's content, so Ryujinx's cache is the one that knows their games.
// Without this the emulator filter never matches and a file-picker load from one
// of them is silently attributed to nothing.
struct CacheAlias {
  const char* profile;
  const char* cacheSource;
};

constexpr CacheAlias kAliases[] = {
    {"Eden", "Ryujinx"},
};

QString cacheSourceFor(const QString& profile) {
  for (const CacheAlias& alias : kAliases) {
    if (profile == QLatin1String(alias.profile)) {
      return QString::fromLatin1(alias.cacheSource);
    }
  }
  return profile;
}

QStringList columnsOf(QSqlDatabase& database, const QString& table) {
  QStringList columns;
  QSqlQuery query(database);
  // The table name comes from the fixed list above, never from user input.
  if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
    return columns;
  }
  while (query.next()) {
    columns.append(query.value(1).toString());
  }
  return columns;
}
} // namespace

QString SessionTitleIndex::normalize(const QString& value) {
  QString folded = value.normalized(QString::NormalizationForm_C).toCaseFolded();
  QString normalized;
  normalized.reserve(folded.size());
  bool pendingSpace = false;
  for (const QChar character : folded) {
    // Punctuation is decoration in a window title ("Game Name [USA] (v1.0)"),
    // and any run of it collapses to a single space.
    if (character.isLetterOrNumber()) {
      if (pendingSpace && !normalized.isEmpty()) {
        normalized.append(QLatin1Char(' '));
      }
      pendingSpace = false;
      normalized.append(character);
    } else {
      pendingSpace = true;
    }
  }
  return normalized;
}

qint64 SessionTitleIndex::cacheChangeToken(QSqlDatabase& database) {
  if (!database.isValid() || !database.isOpen()) {
    return 0;
  }
  // A scan can add, remove or rewrite cache rows, and a rescan rewrites them in place
  // without changing the row count, so neither a timestamp nor a count is enough. The
  // title and path columns are what the index reads, so their combined content is what
  // is fingerprinted. A checksum over these small tables is cheap next to rebuilding the
  // index, and it is the only thing that cannot miss a rename.
  QSqlQuery tables(database);
  if (!tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"))) {
    return 0;
  }
  QSet<QString> present;
  while (tables.next()) {
    present.insert(tables.value(0).toString());
  }
  QCryptographicHash digestHash(QCryptographicHash::Sha256);
  for (const CacheSpec& cache : kCaches) {
    const QString table = QString::fromLatin1(cache.table);
    if (!present.contains(table)) {
      continue;
    }
    const QStringList columns = columnsOf(database, table);
    const QString titleColumn = QString::fromLatin1(cache.titleColumn);
    const QString pathColumn = QString::fromLatin1(cache.pathColumn);
    if (!columns.contains(titleColumn) || !columns.contains(pathColumn)) {
      continue;
    }
    // The identity column is part of the fingerprint as well: a rescan can correct a disc id
    // or a serial without touching the title or the content path, and the attribution
    // adapters resolve through that column.
    QStringList selected{titleColumn, pathColumn};
    const QString identityColumn = QString::fromLatin1(cache.identityColumn);
    if (!identityColumn.isEmpty() && columns.contains(identityColumn)) {
      selected.append(identityColumn);
    }
    QSqlQuery digest(database);
    if (!digest.exec(QStringLiteral("SELECT %1 FROM %2 ORDER BY %1")
                         .arg(selected.join(QStringLiteral(", ")), table))) {
      continue;
    }
    digestHash.addData(table.toUtf8());
    while (digest.next()) {
      for (int column = 0; column < selected.size(); ++column) {
        const QByteArray value = digest.value(column).toString().toUtf8();
        digestHash.addData(QByteArray::number(value.size()) + ':' + value);
      }
    }
  }
  return static_cast<qint64>(qFromBigEndian<quint64>(digestHash.result().constData()) & 0x7fffffffffffffffULL);
}

bool SessionTitleIndex::refresh(QSqlDatabase& database) {
  if (!database.isValid() || !database.isOpen()) {
    return false;
  }
  QVector<Entry> entries;
  QVector<IdentityEntry> identities;
  QSqlQuery exists(database);
  if (!exists.exec(QStringLiteral(
          "SELECT name FROM sqlite_master WHERE type='table'"))) {
    return false;
  }
  QSet<QString> tables;
  while (exists.next()) {
    tables.insert(exists.value(0).toString());
  }
  for (const CacheSpec& cache : kCaches) {
    const QString table = QString::fromLatin1(cache.table);
    if (!tables.contains(table)) {
      continue;
    }
    const QStringList columns = columnsOf(database, table);
    if (!columns.contains(QString::fromLatin1(cache.titleColumn)) ||
        !columns.contains(QString::fromLatin1(cache.pathColumn))) {
      continue;
    }
    const QString identityColumn = QString::fromLatin1(cache.identityColumn);
    const bool hasIdentity = !identityColumn.isEmpty() && columns.contains(identityColumn);
    QStringList selected{QString::fromLatin1(cache.titleColumn),
                         QString::fromLatin1(cache.pathColumn)};
    if (hasIdentity) {
      selected.append(identityColumn);
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1 FROM %2").arg(selected.join(QStringLiteral(", ")),
                                                         table));
    if (!query.exec()) {
      return false;
    }
    const QString source = QString::fromLatin1(cache.source);
    while (query.next()) {
      const QString title = query.value(0).toString().trimmed();
      const QString path = query.value(1).toString().trimmed();
      if (path.isEmpty()) {
        continue;
      }
      if (!title.isEmpty()) {
        const Entry entry{.title = title, .gamePath = path, .emulator = source};
        entries.append(entry);
      }
      // The emulator's own identity for the same game, so an attribution adapter can resolve
      // what the emulator recorded to the content path a session is recorded against.
      if (hasIdentity) {
        const QString identity = query.value(2).toString().trimmed();
        if (!identity.isEmpty()) {
          const IdentityEntry identityEntry{
              .identity = identity, .gamePath = path, .emulator = source};
          identities.append(identityEntry);
        }
      }
    }
  }
  m_entries = entries;
  m_identities = identities;
  return true;
}

QString SessionTitleIndex::titleForPath(const QString& gamePath) const {
  QString found;
  for (const Entry& entry : m_entries) {
    if (entry.gamePath != gamePath) continue;
    if (!found.isEmpty() && found != entry.title) return {};
    found = entry.title;
  }
  return found;
}

QString SessionTitleIndex::pathForWindowTitle(const QString& windowTitle,
                                              const QString& emulator) const {
  const QString normalizedTitle = normalize(windowTitle);
  if (normalizedTitle.size() < kMinimumMatchLength) {
    return {};
  }
  const auto consider = [&emulator](const Entry& entry) {
    return emulator.isEmpty() || entry.emulator == cacheSourceFor(emulator);
  };
  // An exact title is just the strongest form of a whole-name match, so it goes
  // through the same counting instead of short-circuiting. Returning the first
  // exact sighting would attribute the window to whichever cache row happened to
  // come first when two different games share a title, or when one game appears
  // in two caches under different content paths.
  QString found;
  int matches = 0;
  for (const Entry& entry : m_entries) {
    if (!consider(entry)) {
      continue;
    }
    const QString normalizedName = normalize(entry.title);
    if (normalizedName.size() < kMinimumMatchLength) {
      continue;
    }
    // A whole-name match inside the decorated title, with the name on a word
    // boundary so "Mario" never matches inside "Marioland".
    const QString padded = QStringLiteral(" ") + normalizedTitle + QStringLiteral(" ");
    if (!padded.contains(QStringLiteral(" ") + normalizedName + QStringLiteral(" "))) {
      continue;
    }
    if (entry.gamePath == found) {
      continue;
    }
    if (++matches > 1) {
      return {};
    }
    found = entry.gamePath;
  }
  return matches == 1 ? found : QString{};
}

QString SessionTitleIndex::pathForGameId(const QString& identity, const QString& emulator) const {
  const QString wanted = identity.trimmed();
  if (wanted.isEmpty()) {
    return {};
  }
  const auto consider = [&emulator](const IdentityEntry& entry) {
    return emulator.isEmpty() || entry.emulator == cacheSourceFor(emulator);
  };
  // Counted rather than short-circuited, for the same reason a window title is: two games
  // recorded under one identity, or one game under two content paths, is an ambiguity, and
  // picking whichever came first would attribute play to the wrong game.
  QString found;
  int matches = 0;
  for (const IdentityEntry& entry : m_identities) {
    if (!consider(entry) || entry.identity.compare(wanted, Qt::CaseInsensitive) != 0) {
      continue;
    }
    if (entry.gamePath == found) {
      continue;
    }
    if (++matches > 1) {
      return {};
    }
    found = entry.gamePath;
  }
  return matches == 1 ? found : QString{};
}
