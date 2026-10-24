#pragma once

#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

// Attributes a session for a game an emulator loaded from its own file picker.
// Those loads never name the game on the command line, but the emulator names it
// in the window title, and Omakade's own source scan has already recorded the
// game titles it knows about.
//
// The index reads the title and content path of every emulator cache table that
// it finds in the library database. Nothing is written: the caches belong to the
// sources, and a recorder that rewrote them would be a second owner of the same
// data.
//
// Matching is deliberately conservative. An exact title match is used, and so is
// a game name that appears whole inside a decorated window title such as
// "PCSX2 1.7.5 - Game Name", but only when exactly one known game matches. Two
// candidates, or a name too short to be distinctive, produce no match at all, so
// an ambiguous window is left untracked rather than attributed to the wrong game.
class SessionTitleIndex final {
public:
  // One known game: the title the library shows and the content path a session
  // should be recorded against.
  struct Entry {
    QString title;
    QString gamePath;
    QString emulator;
  };

  // Reads the caches from the caller's open connection. Returns false only when
  // the database is unusable; a database with no caches is a valid, empty index.
  bool refresh(QSqlDatabase& database);

  // A cheap change detector over the cache tables this index reads, so a caller can
  // skip a rebuild when nothing it cares about has changed. Watching the database file's
  // timestamp does not work: the recorder shares that file and its own writes move the
  // write-ahead log's timestamp, which would rebuild the index on every poll.
  [[nodiscard]] static qint64 cacheChangeToken(QSqlDatabase& database);

  [[nodiscard]] bool isEmpty() const { return m_entries.isEmpty(); }
  [[nodiscard]] int size() const { return m_entries.size(); }

  // The game a window title names, or an empty path when nothing matches
  // confidently. emulator, when non-empty, restricts the search to that
  // emulator's own cache.
  [[nodiscard]] QString pathForWindowTitle(const QString& windowTitle,
                                           const QString& emulator = {}) const;

  // The game an emulator's own identity names, which is what an attribution adapter reads
  // out of the emulator's records: a disc id, a title id, a serial. Compared without case,
  // because the emulators disagree about it and no real identity set differs only by case.
  // An identity the library does not know, or knows under two different content paths, is
  // refused with an empty answer rather than guessed at.
  [[nodiscard]] QString pathForGameId(const QString& identity, const QString& emulator = {}) const;

  // The title the library shows for a content path, when exactly one title is known for it.
  [[nodiscard]] QString titleForPath(const QString& gamePath) const;

  // Window titles are decorated: an emulator version, a separator, a status, and
  // sometimes a suffix. Comparing normalized text ignores that decoration.
  [[nodiscard]] static QString normalize(const QString& value);

  // A name has to be long enough that a substring match means something.
  static constexpr int kMinimumMatchLength = 6;

private:
  // One identity an emulator uses for a game, and the content path it resolves to.
  struct IdentityEntry {
    QString identity;
    QString gamePath;
    QString emulator;
  };

  QVector<Entry> m_entries;
  QVector<IdentityEntry> m_identities;
};
