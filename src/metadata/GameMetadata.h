#pragma once

#include "metadata/GameInsightsService.h"
#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QElapsedTimer>
#include <QAbstractItemModel>
#include <QSet>
#include <QTimer>
#include <QObject>
#include <QQueue>
#include <QSqlDatabase>
#include <QVariantMap>

class UnifiedGameModel;

// One persistent identity per local game. Provider IDs remain separate.
class GameMetadata final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool selectedBusy READ selectedBusy NOTIFY changed)
  Q_PROPERTY(bool selectedWritePending READ selectedWritePending NOTIFY changed)
  Q_PROPERTY(QString selectedStatus READ selectedStatus NOTIFY changed)
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
  // The filtered view the user is looking at. Games on screen are identified first, so opening
  // a console fills it in rather than waiting for the rest of the library.
  void setVisibleLibrary(QAbstractItemModel* visible);
  void setCacheLimitMb(int megabytes);
  QVariantMap entry(const QString& key) const { return m_entries.value(key); }
  bool reviewWritable() const { return !busy() && m_pendingWrites.isEmpty(); }
  void reloadReviewEntry(const QString& key);
  Q_INVOKABLE void retryReviewGames(const QVariantList& games);
  bool busy() const { return m_busy || !m_queue.isEmpty() || m_secrets.isRunning(); }
  bool hasGridKey() const { return !m_gridKey.isEmpty(); }
  int pending() const { return m_queue.size() + (m_busy ? 1 : 0); }
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void refreshSelected();
  bool selectedBusy() const;
  bool selectedWritePending() const {
    return m_pendingWrites.contains(m_selected.value("metadataKey").toString());
  }
  QString selectedStatus() const;
  QString status() const { return m_status; }
  QVariantMap current() const;
  QVariantList candidates() const {
    return m_active.value("metadataKey") == m_selected.value("metadataKey") ? m_candidates
                                                                            : QVariantList{};
  }
  QVariantList covers() const {
    return m_active.value("metadataKey") == m_selected.value("metadataKey") ? m_covers
                                                                            : QVariantList{};
  }
  Q_INVOKABLE void inspect(const QVariantMap& game);
  // While someone is identifying a game by hand, the background pass stands down. Without this
  // the queue keeps the service busy and every manual control stays disabled, because they are
  // all gated on the same busy state.
  Q_INVOKABLE void setEditing(bool editing);
  Q_INVOKABLE void refreshLibrary();
  // Continues the library pass on its own: after the library settles, whenever games are added,
  // and whenever the view changes. Stopping it by hand keeps it stopped until the next launch.
  // Identification depends on how titles are cleaned, which platforms count, and how a match is
  // accepted. Raise this whenever any of those change: every entry decided by older rules is
  // then re-identified on the next update, instead of waiting out the ordinary freshness window
  // with a stale answer. Forgetting leaves a library stuck on answers the rules would no longer
  // give, so matchingRulesFingerprint below fails the build's tests until this is raised.
  //   2  dump tags, sorted articles, tie-breaking between equal titles
  //   3  regional platforms, accents, publisher prefixes, catalogue numbers
  //   4  ambiguous editions require identification; recheck older automatic IDs
  //   5  exact title/alias lookup before declaring broad search results ambiguous
  static constexpr int kMatchVersion = 6;
  // Everything the identification rules depend on, folded into one value. A test pins it, so a
  // change to any rule fails until kMatchVersion is raised alongside it.
  [[nodiscard]] static QByteArray matchingRulesFingerprint();
  // How long a rating stays fresh before it is fetched again.
  static constexpr qint64 kRatingFreshnessSeconds = 30 * 86400;
  // True when a stored entry should be identified again: either the rules that decided it have
  // changed, or its rating has simply aged out.
  [[nodiscard]] static bool needsIdentifying(const QVariantMap& saved, qint64 now);
  // The order games are identified in. Anything on screen comes first, and the rest is taken a
  // system at a time in turn, so a shelf of 1,387 SNES ROMs cannot starve the eight N64 games
  // behind it. Pure so the ordering can be tested without a network.
  [[nodiscard]] static QList<QVariantMap> orderForIdentification(const QList<QVariantMap>& games,
                                                                 const QSet<QString>& onScreen);
  // A downloaded portrait replaces the artwork a game already has, so it is only worth
  // fetching when that artwork is missing or cannot serve as a cover. Takes the system and the
  // path to the source's own art.
  [[nodiscard]] static bool wantsPortraitCover(const QString& system, const QString& source,
                                               const QString& sourceCover);
  // Artwork at least as tall as 4:3 already works as a cover and is left alone.
  static constexpr double kPortraitAspectLimit = 0.8;
  // How far a SteamGridDB release year may sit from IGDB's before the entry stops counting as
  // the same game. Three years covers a staggered regional release and a console port of an
  // older arcade game without reaching the sequels that share a name.
  static constexpr int kGridYearTolerance = 3;
  // Which SteamGridDB game to take covers from, given the candidates its search returned as
  // maps of id, title and year. Returns 0 when no candidate is clearly the right one. Pure so
  // the rule can be tested without a network.
  [[nodiscard]] static qint64 chooseGridMatch(const QVariantList& candidates, const QString& title,
                                              int year, const QString& system = {});
  // A game that has been looked up and not matched is not asked about again for a day, so a
  // library of imports does not spend every launch re-asking about the same games. Raise
  // kCoverRulesVersion whenever chooseGridMatch or wantsPortraitCover changes: without it a
  // rules fix reaches a library only as each entry ages out, and someone testing the fix on the
  // day they make it sees nothing happen at all and concludes it does not work.
  //   1  exact title with the year as a tie-breaker, replacing exact title and exact year
  //   2  publisher prefixes, and unconfirmed grid selections dropped rather than trusted
  //   4  explicit long-vowel spellings and SNES catalogue qualifiers
  static constexpr int kCoverRulesVersion = 4;
  static constexpr qint64 kCoverAttemptBackoffSeconds = 86400;
  [[nodiscard]] static bool needsCoverAttempt(const QVariantMap& saved, qint64 now);
  // A licensed game is often catalogued with its publisher in front: IGDB calls a cartridge
  // labelled Goof Troop "Disney's Goof Troop". Returns an already normalized title with that
  // prefix removed, or unchanged when it has none.
  [[nodiscard]] static QString withoutBrandPrefix(const QString& normalized);
  Q_INVOKABLE QString searchTitle(const QString& title) const;
  Q_INVOKABLE void search(const QString& title);
  Q_INVOKABLE void chooseMatch(int index);
  Q_INVOKABLE void rejectMatch();
  Q_INVOKABLE void findCovers();
  // Looks SteamGridDB up under a name the user typed, for the games whose catalogue name shares
  // nothing with the cartridge: Dragon Quest V is filed as Hand of the Heavenly Bride.
  Q_INVOKABLE void searchCovers(const QString& title);
  // Drops the SteamGridDB game a search or a mis-click settled on, so the next pass looks again.
  Q_INVOKABLE void clearGridSelection();
  Q_INVOKABLE void chooseGridGame(int index);
  Q_INVOKABLE void chooseCover(int index);
  Q_INVOKABLE void storeGridKey(QString key);
  Q_INVOKABLE void removeGridKey();
  Q_INVOKABLE void testGridConnection();
  Q_INVOKABLE void clearPortraitCache();
  static QString normalizedTitle(QString title);
  // The IGDB platforms a system's games can be listed under. A Japanese release is often
  // catalogued under the regional machine rather than the western one.
  static QList<int> platformIds(const QString& system);
  static bool equivalentTitle(const QString& left, const QString& right);
  static QByteArray discoveryQuery(const QString& title, const QString& system);
  static QByteArray searchQuery(const QString& title, const QString& system, bool cleanRomTags = true);
  static QByteArray aliasSearchQuery(const QString& title, const QString& system);
  static QVariantList parseMatches(const QByteArray& data, const QList<int>& platforms);
  static QVariantList parseCovers(const QByteArray& data);
  static bool trustedImageUrl(const QUrl& url);
  // IGDB platform ids to readable names for games whose source carries no system
  // of its own, like Steam. Unknown ids come back as empty and are skipped.
  Q_INVOKABLE static QStringList platformNames(const QVariantList& ids);
signals:
  void changed();
  void entryChanged(const QString& key, const QVariantMap& previous);
  void portraitSelected(const QString& key);

private:
  friend class CoreTests;
  void trimPortraitCache();
  bool persist(const QString& key, const QVariantMap& value);
  void enqueue(const QVariantMap& game);
  void queueSelected(bool force = false);
  QHash<QString, qint64> m_detailAttempts;
  QHash<QString, QString> m_detailErrors;
  QHash<QString, QVariantMap> m_pendingWrites;
  bool m_aliasRetried = false;
  bool m_discoveryRetried = false;
  void next();
  void finish(const QString& message);
  void requestIgdb(QByteArray query, QString endpoint, QString stage);
  void matchResult(const QByteArray& data, const QString& error);
  void acceptMatch(const QVariantMap& match);
  // Shared by findCovers and searchCovers: an empty title uses the catalogue's own.
  void beginCoverSearch(const QString& typedTitle);
  void gridSearch();
  static QStringList artworkSearchTitles(const QVariantMap& entry);
  static bool canSharePortrait(const QVariantMap& target, const QVariantMap& donor);
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
  // Each provider is paced on its own, so the queue does not need a blanket pause between games.
  QElapsedTimer m_sinceGridRequest;
  bool m_stoppedByHand = false;
  bool m_editing = false;
  QQueue<QVariantMap> m_pausedQueue;
  QAbstractItemModel* m_visible = nullptr;
  QTimer m_settle;
  void continueLibraryPass();
  void promoteVisibleGames();
  QQueue<QVariantMap> m_queue;
  QVariantMap m_selected, m_active;
  QVariantList m_candidates, m_covers;
  QString m_status;
  QString m_candidateProvider;
  QHash<QByteArray, QByteArray> m_queryCache;
  QByteArray m_queryKey;
  QString m_igdbStage;
  // A ROM set can number its files, as 1636 - Pokemon Fire Red. Searching for that finds
  // nothing, so a failed search is tried once more without the number. Held here so a title
  // that genuinely starts with a number is only ever searched for as written first.
  QString m_numberedRetryTitle;
  // The same idea for SteamGridDB: searching for "Disney's Goof Troop" returns ten other Disney
  // games and not that one, because the catalogue files it as plain Goof Troop. The prefix is
  // only dropped after the title as written has failed, so a game whose name really starts that
  // way is searched for as written first.
  QString m_brandRetryTitle;
  QStringList m_artworkTitles;
  int m_artworkTitleIndex = 0;
  // A name typed by hand in the cover panel, used instead of the catalogue's own title.
  QString m_manualSearchTitle;
  // A grid game picked by hand is held here rather than stored. Storing it on the click meant a
  // glance at the wrong candidate pinned the game to it for good: the background pass short
  // circuits on a stored id, so it would go on to fetch that game's artwork by itself and there
  // was no way back. It is written only once a cover from it is actually taken.
  qint64 m_pendingGridId = 0;
  bool m_cancelled = false;
  bool m_busy = false;
  bool m_manual = false;
  qint64 m_downloadId = 0;
};
