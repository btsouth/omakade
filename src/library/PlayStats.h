#pragma once

#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class UnifiedGameModel;

// The figures behind the Stats screen and the year-in-review card.
//
// Two rules govern everything here, and both are load-bearing:
//
//  - Anything that describes the library comes from the model's own roles. There is no
//    second playtime calculation, so the stats and the library cannot disagree about what
//    a game is worth. Imported counters cannot be split by period, so they are reported as
//    library totals and never presented as this period's play.
//  - Anything dated comes from recorded sessions only, and recording starts when it
//    starts. Every period figure is therefore reported together with the window it really
//    covers, so the card can say "recorded since 8 September" instead of implying the
//    whole year was watched.
//
// `period` is "year" (the calendar year named by `year`, in local time) or "all". A session
// belongs to the period it started in, and its whole recorded time counts there; only the
// hour and weekday distributions are clipped to the period, so no figure credits time
// outside the window it claims. Nothing is recomputed while the view is closed.
class PlayStats final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active WRITE setActive)
  Q_PROPERTY(QString period READ period WRITE setPeriod NOTIFY changed)
  Q_PROPERTY(int year READ year WRITE setYear NOTIFY changed)
  Q_PROPERTY(QString periodLabel READ periodLabel NOTIFY changed)
  Q_PROPERTY(QString windowNote READ windowNote NOTIFY changed)
  Q_PROPERTY(qint64 recordingStartsAt READ recordingStartsAt NOTIFY changed)
  Q_PROPERTY(QVariantMap headline READ headline NOTIFY changed)
  Q_PROPERTY(QVariantList bySource READ bySource NOTIFY changed)
  Q_PROPERTY(QVariantList bySystem READ bySystem NOTIFY changed)
  Q_PROPERTY(QVariantList byHour READ byHour NOTIFY changed)
  Q_PROPERTY(QVariantList byWeekday READ byWeekday NOTIFY changed)
  Q_PROPERTY(QVariantList topGames READ topGames NOTIFY changed)
  Q_PROPERTY(QVariantMap sessionShape READ sessionShape NOTIFY changed)
  Q_PROPERTY(QVariantMap streaks READ streaks NOTIFY changed)
  Q_PROPERTY(QVariantMap achievements READ achievements NOTIFY changed)
  Q_PROPERTY(QVariantMap backlog READ backlog NOTIFY changed)
  Q_PROPERTY(QVariantMap library READ library NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)

public:
  PlayStats(UnifiedGameModel* games, const QString& databasePath, QObject* parent = nullptr);
  ~PlayStats() override;

  [[nodiscard]] bool active() const { return m_active; }
  void setActive(bool value);

  [[nodiscard]] QString period() const { return m_period; }
  void setPeriod(const QString& value);

  [[nodiscard]] int year() const { return m_year; }
  void setYear(int value);

  // "2026" or "All time", for headings and the card.
  [[nodiscard]] QString periodLabel() const { return m_periodLabel; }
  // A sentence naming the window the recorded figures really cover, empty when the period
  // is fully covered by recording.
  [[nodiscard]] QString windowNote() const { return m_windowNote; }
  // Epoch seconds of the earliest session ever recorded, 0 when nothing has been recorded.
  [[nodiscard]] qint64 recordingStartsAt() const { return m_recordingStartsAt; }

  // Recorded time, sessions, days, games, the top game with its share of that time, and the
  // library's own reported totals beside them.
  [[nodiscard]] QVariantMap headline() const { return m_headline; }
  // Recorded time per source (Ryujinx, Steam, ...): entries of name, seconds, sessions,
  // games, share.
  [[nodiscard]] QVariantList bySource() const { return m_bySource; }
  // The same, grouped by console where the source is a dedicated emulator and by source
  // otherwise: entries of name, seconds, share, console.
  [[nodiscard]] QVariantList bySystem() const { return m_bySystem; }
  // 24 entries of hour, seconds; 7 entries of weekday (0 = Monday), seconds.
  [[nodiscard]] QVariantList byHour() const { return m_byHour; }
  [[nodiscard]] QVariantList byWeekday() const { return m_byWeekday; }
  [[nodiscard]] QVariantList topGames() const { return m_topGames; }
  // count, totalSeconds, averageSeconds, longestSeconds, longestTitle, longestStartedAt,
  // overTwoHours, buckets (label, count).
  [[nodiscard]] QVariantMap sessionShape() const { return m_sessionShape; }
  // daysPlayed, longestRun, currentRun, daysOff, firstDay, lastDay.
  [[nodiscard]] QVariantMap streaks() const { return m_streaks; }
  // unlockedInPeriod, unlockedTotal, known, rate, rarest (title, rarity, source, appId,
  // gameTitle), bySource (source, unlocked).
  [[nodiscard]] QVariantMap achievements() const { return m_achievements; }
  // firstTimeGames, oneAndDone, returns, longestGapDays, returnsList (title, gapDays).
  [[nodiscard]] QVariantMap backlog() const { return m_backlog; }
  // games, systems, genres (genre, seconds, share), completions (status, count),
  // topRated (title, rating, ratingCount, source).
  [[nodiscard]] QVariantMap library() const { return m_library; }
  [[nodiscard]] QString error() const { return m_error; }

  // Recomputes everything now. Called when the view opens and by the tests.
  Q_INVOKABLE void refresh();

signals:
  void changed();

private:
  struct RecordedSession {
    qint64 startedAt = 0;
    qint64 endedAt = 0;
    qint64 seconds = 0;
    QString source;
    QString path;
  };

  void scheduleRefresh();
  void recompute();

  // One row of the library, reduced to what the figures here need. Read once per recompute
  // from the model's roles; the model is the only source of playtime, and it is already
  // reconciled with the recorder.
  struct LibraryGame {
    QString identity; // source and app id, so a game with several installations is one game
    QString title;
    QString source;
    QString system;
    QString appId;
    QStringList paths; // every installation path a session for this game can be recorded under
    QStringList genres;
    qint64 playtimeSeconds = 0;
    QString completion;
    int rating = 0;
    int ratingCount = 0;
    bool portal = false;
  };
  [[nodiscard]] QVector<LibraryGame> readLibrary() const;

  UnifiedGameModel* m_games = nullptr;
  QString m_connection;
  QSqlDatabase m_database;
  bool m_valid = false;
  bool m_active = false;
  bool m_refreshPending = false;
  QString m_period = QStringLiteral("year");
  int m_year = 0;
  QString m_error;
  QString m_periodLabel;
  QString m_windowNote;
  qint64 m_recordingStartsAt = 0;
  QVariantMap m_headline;
  QVariantList m_bySource;
  QVariantList m_bySystem;
  QVariantList m_byHour;
  QVariantList m_byWeekday;
  // The most played games of the period, most first, capped at five.
  QVariantList m_topGames;
  QVariantMap m_sessionShape;
  QVariantMap m_streaks;
  QVariantMap m_achievements;
  QVariantMap m_backlog;
  QVariantMap m_library;
};
