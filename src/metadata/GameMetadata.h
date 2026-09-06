#pragma once

#include "metadata/GameInsightsService.h"
#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSqlDatabase>
#include <QVariantMap>

class UnifiedGameModel;

// One persistent identity per local game. Provider IDs remain separate.
class GameMetadata final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(int pending READ pending NOTIFY changed)
  Q_PROPERTY(bool hasGridKey READ hasGridKey NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
  Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
  Q_PROPERTY(QVariantList covers READ covers NOTIFY changed)
public:
  GameMetadata(const QString& databasePath, GameInsightsService* insights,
               QObject* parent = nullptr, QNetworkAccessManager* network = nullptr);
  ~GameMetadata() override;
  void setLibrary(UnifiedGameModel* library);
  void setCacheLimitMb(int megabytes);
  QVariantMap entry(const QString& key) const { return m_entries.value(key); }
  bool busy() const { return m_busy || !m_queue.isEmpty() || m_secrets.isRunning(); }
  bool hasGridKey() const { return !m_gridKey.isEmpty(); }
  int pending() const { return m_queue.size() + (m_busy ? 1 : 0); }
  Q_INVOKABLE void cancel();
  QString status() const { return m_status; }
  QVariantMap current() const { return entry(m_selected.value("metadataKey").toString()); }
  QVariantList candidates() const {
    return m_active.value("metadataKey") == m_selected.value("metadataKey") ? m_candidates
                                                                            : QVariantList{};
  }
  QVariantList covers() const {
    return m_active.value("metadataKey") == m_selected.value("metadataKey") ? m_covers
                                                                            : QVariantList{};
  }
  Q_INVOKABLE void inspect(const QVariantMap& game);
  Q_INVOKABLE void refreshLibrary();
  // Identification depends on how titles are cleaned and how a match is accepted. Raise this
  // whenever those rules change: every entry decided by older rules is then re-identified on
  // the next update, instead of waiting out the ordinary freshness window with a stale answer.
  static constexpr int kMatchVersion = 2;
  // How long a rating stays fresh before it is fetched again.
  static constexpr qint64 kRatingFreshnessSeconds = 30 * 86400;
  // True when a stored entry should be identified again: either the rules that decided it have
  // changed, or its rating has simply aged out.
  [[nodiscard]] static bool needsIdentifying(const QVariantMap& saved, qint64 now);
  Q_INVOKABLE void search(const QString& title);
  Q_INVOKABLE void chooseMatch(int index);
  Q_INVOKABLE void rejectMatch();
  Q_INVOKABLE void findCovers();
  Q_INVOKABLE void chooseGridGame(int index);
  Q_INVOKABLE void chooseCover(int index);
  Q_INVOKABLE void storeGridKey(QString key);
  Q_INVOKABLE void removeGridKey();
  Q_INVOKABLE void testGridConnection();
  Q_INVOKABLE void clearPortraitCache();
  static QString normalizedTitle(QString title);
  static int platformId(const QString& system);
  static QByteArray searchQuery(const QString& title, const QString& system);
  static QVariantList parseMatches(const QByteArray& data, int platform);
  static QVariantList parseCovers(const QByteArray& data);
  static bool trustedImageUrl(const QUrl& url);
signals:
  void changed();
  void entryChanged(const QString& key);
  void portraitSelected(const QString& key);

private:
  friend class CoreTests;
  void trimPortraitCache();
  void persist(const QString& key, const QVariantMap& value);
  void enqueue(const QVariantMap& game);
  void next();
  void finish(const QString& message);
  void requestIgdb(QByteArray query, QString endpoint, QString stage);
  void matchResult(const QByteArray& data, const QString& error);
  void acceptMatch(const QVariantMap& match);
  void gridSearch();
  void gridCovers(qint64 id);
  void get(const QUrl& url, const QString& stage);
  void response(const QByteArray& data, const QString& stage);
  void secretOperation(int action, QByteArray value = {});
  QString key() const { return m_active.value("metadataKey").toString(); }
  UnifiedGameModel* m_library = nullptr;
  GameInsightsService* m_insights;
  QSqlDatabase m_database;
  QString m_connection;
  QString m_cacheRoot;
  qint64 m_cacheLimitBytes = 1024LL * 1024 * 1024;
  QHash<QString, QVariantMap> m_entries;
  QNetworkAccessManager* m_network;
  QFutureWatcher<InsightsSecretResult> m_secrets;
  QByteArray m_gridKey;
  QQueue<QVariantMap> m_queue;
  QVariantMap m_selected, m_active;
  QVariantList m_candidates, m_covers;
  QString m_status;
  QString m_candidateProvider;
  QHash<QByteArray, QByteArray> m_queryCache;
  QByteArray m_queryKey;
  QString m_igdbStage;
  bool m_cancelled = false;
  bool m_busy = false;
  bool m_manual = false;
  qint64 m_downloadId = 0;
};
