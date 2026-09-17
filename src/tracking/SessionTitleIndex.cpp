#include "tracking/SessionTitleIndex.h"

#include <QSet>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QtGlobal>

namespace {
// Emulator caches that carry both a display name and a content path. Each entry
// is the table, the columns holding them, and the Omakade source the cache
// belongs to. A cache whose columns differ is skipped instead of guessed at.
struct CacheSpec {
  const char* table;
  const char* titleColumn;
  const char* pathColumn;
  const char* source;
};

constexpr CacheSpec kCaches[] = {
    {"retroarch_games", "name", "content_path", "RetroArch"},
    {"pcsx2_games", "name", "path", "PCSX2"},
    {"ryujinx_games", "name", "path", "Ryujinx"},
    {"dolphin_games", "name", "path", "Dolphin"},
    {"cemu_games", "name", "path", "Cemu"},
    {"shadps4_games", "name", "path", "shadPS4"},
    {"xenia_games", "name", "path", "Xenia"},
};

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

bool SessionTitleIndex::refresh(QSqlDatabase& database) {
  if (!database.isValid() || !database.isOpen()) {
    return false;
  }
  QVector<Entry> entries;
  QHash<QString, Entry> byTitle;
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
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1, %2 FROM %3")
                      .arg(QString::fromLatin1(cache.titleColumn),
                           QString::fromLatin1(cache.pathColumn), table));
    if (!query.exec()) {
      continue;
    }
    while (query.next()) {
      const QString title = query.value(0).toString().trimmed();
      const QString path = query.value(1).toString().trimmed();
      if (title.isEmpty() || path.isEmpty()) {
        continue;
      }
      const Entry entry{.title = title,
                        .gamePath = path,
                        .emulator = QString::fromLatin1(cache.source)};
      entries.append(entry);
      const QString key = normalize(title);
      // Keep the first sighting. Cache order is stable, so a repeat across
      // caches does not make the answer depend on insertion order.
      if (!key.isEmpty() && !byTitle.contains(key)) {
        byTitle.insert(key, entry);
      }
    }
  }
  m_entries = entries;
  m_byExactTitle = byTitle;
  return true;
}

QString SessionTitleIndex::pathForWindowTitle(const QString& windowTitle,
                                              const QString& emulator) const {
  const QString normalizedTitle = normalize(windowTitle);
  if (normalizedTitle.size() < kMinimumMatchLength) {
    return {};
  }
  const auto consider = [&emulator](const Entry& entry) {
    return emulator.isEmpty() || entry.emulator == emulator;
  };
  const auto exact = m_byExactTitle.constFind(normalizedTitle);
  if (exact != m_byExactTitle.cend() && consider(exact.value())) {
    return exact.value().gamePath;
  }
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
