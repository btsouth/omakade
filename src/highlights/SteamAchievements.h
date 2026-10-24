#pragma once

#include "sources/steam/ValveKeyValues.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

// Notices a Steam achievement unlocked during play. The Steam client keeps each game's
// unlocks in appcache/stats/UserGameStats_<account>_<appid>.bin, with the time of each one;
// this reads the running game's files every couple of seconds and reports an unlock that
// is new since it started watching.
//
// Only an unlock stamped within the last few seconds counts. Steam can rewrite these files
// at other times, such as when it syncs at startup, and a clip saved for an old unlock
// would show the wrong moment, so such an unlock is noted and never clipped.
class SteamAchievements final : public QObject {
  Q_OBJECT

public:
  explicit SteamAchievements(QStringList statsDirectories, QObject* parent = nullptr);

  // appcache/stats in each place the Steam client lives: native, the ~/.steam link and Flatpak.
  [[nodiscard]] static QStringList defaultDirectories();
  // "stat/bit" for every unlocked achievement in a UserGameStats file, with its unlock time.
  [[nodiscard]] static QHash<QString, qint64> unlocks(const ValveKeyValues& stats);
  // The achievement's name in the schema, in `language` when it has one, else English.
  [[nodiscard]] static QString displayName(const ValveKeyValues& schema, const QString& appId,
                                           const QString& unlockKey, const QString& language);
  // Steam's name for the system language, such as "german". English when it has none.
  [[nodiscard]] static QString steamLanguage();

  // Starts watching a game's files; an empty id stops.
  void watch(const QString& appId);
  [[nodiscard]] QString appId() const { return m_appId; }

  // How recent an unlock must be to be clipped.
  qint64 freshSeconds = 20;
  std::function<qint64()> now = [] { return QDateTime::currentSecsSinceEpoch(); };

  // Reads the files now rather than waiting for the next poll.
  void check();

signals:
  // time is when Steam says the achievement was unlocked.
  void unlocked(const QString& name, qint64 time);

private:
  [[nodiscard]] QStringList files() const;

  QStringList m_directories;
  QString m_appId;
  QTimer m_poll;
  QHash<QString, QDateTime> m_modified;
  QHash<QString, qint64> m_known;
};
