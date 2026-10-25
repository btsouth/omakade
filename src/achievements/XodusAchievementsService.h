#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

class QProcess;
class QTimer;

struct XodusAchievementRecord {
  QString apiName;
  QString title;
  QString description;
  QString iconUrl;
  bool unlocked = false;
  qint64 unlockTime = 0;
  bool hidden = false;
  double progress = 0.0;
};

struct XodusAchievementsResult {
  bool valid = false;
  QVector<XodusAchievementRecord> achievements;
  int unlocked = 0;
  // Nexus could not read every achievement.
  bool partial = false;
};

// Read-only Xbox achievements for Xodus Nexus games. Nexus's own backend, signed in to the
// player's Microsoft account, reports them; Omakade only reads and caches what it says and
// never signs in, stores tokens or unlocks anything.
class XodusAchievementsService final : public QObject {
  Q_OBJECT
  // hasApiKey and username match the Steam and RetroAchievements services, which the game page
  // binds the same way. Here the "key" is a Nexus backend that can be asked.
  Q_PROPERTY(QString username READ username CONSTANT)
  Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY accountChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString state READ state NOTIFY statusChanged)

public:
  explicit XodusAchievementsService(const QString& databasePath, QObject* parent = nullptr);
  ~XodusAchievementsService() override;

  [[nodiscard]] QString username() const { return {}; }
  [[nodiscard]] bool hasApiKey() const;
  [[nodiscard]] bool busy() const { return m_process != nullptr; }
  [[nodiscard]] QString statusText() const { return m_statusText; }
  [[nodiscard]] QString state() const { return m_state; }

  // Cached rows are kept under this key so a Nexus game ID cannot meet a Steam app ID.
  [[nodiscard]] static QString cacheKey(const QString& gameId);
  [[nodiscard]] static XodusAchievementsResult parse(const QByteArray& json, const QString& titleId);
  // Testing: where Nexus's profile is, instead of the one it would use.
  void setProfileRoot(const QString& root) { m_profileRoot = root; }

  Q_INVOKABLE void refreshAchievements(const QString& gameId);
  Q_INVOKABLE void refreshAchievementsIfStale(const QString& gameId);

signals:
  void accountChanged();
  void busyChanged();
  void statusChanged();
  void achievementsUpdated(const QString& key);

private:
  [[nodiscard]] QString profileRoot() const;
  [[nodiscard]] QString installPath(const QString& gameId) const;
  void finish();
  bool persist(const QString& gameId, const XodusAchievementsResult& result, qint64 updatedAt);
  [[nodiscard]] bool cached(const QString& gameId) const;
  void setStatus(const QString& state, const QString& text);

  QSqlDatabase m_database;
  QString m_connectionName;
  QString m_profileRoot;
  QProcess* m_process = nullptr;
  QTimer* m_timeout = nullptr;
  QString m_gameId;
  QString m_titleId;
  QString m_pendingGameId;
  QString m_shownGameId;
  QString m_statusText;
  QString m_state;
};
