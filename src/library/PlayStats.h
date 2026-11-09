#pragma once

#include <QDate>
#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
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
// Calendar totals and distributions share the same clipped session intervals. A session the
// recorder kept play intervals for is placed by those intervals, so paused time lands nowhere.
// Older sessions only have a start, an end and a total; their play is spread evenly over that
// span, and when the span is longer than the play the split is labelled as an estimate.
//
// The figures are computed from a snapshot: the library rows are read on the GUI thread (the
// model lives there), and the session history is read and aggregated on a worker thread. The
// first time the screen opens, and on an explicit refresh(), the work runs where it is called
// so the figures are there when the caller reads them.
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
  // One row of the library, reduced to what the figures here need. Read on the GUI thread from
  // the model's roles; the model is the only source of playtime, and it is already reconciled
  // with the recorder.
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

  // Everything one computation needs. It holds values only, so a worker can use it while the
  // library keeps changing.
  struct Snapshot {
    QVector<LibraryGame> library;
    QString databasePath;
    QString period = QStringLiteral("year");
    int year = 0;
    QDate today;
  };

  struct Figures {
    quint64 generation = 0;
    QString error;
    QString periodLabel;
    QString windowNote;
    qint64 recordingStartsAt = 0;
    QVariantMap headline;
    QVariantList bySource;
    QVariantList bySystem;
    QVariantList byHour;
    QVariantList byWeekday;
    QVariantList topGames;
    QVariantMap sessionShape;
    QVariantMap streaks;
    QVariantMap achievements;
    QVariantMap backlog;
    QVariantMap library;
  };

  PlayStats(UnifiedGameModel* games, const QString& databasePath, QObject* parent = nullptr);
  ~PlayStats() override;

  [[nodiscard]] bool active() const { return m_active; }
  void setActive(bool value);

  [[nodiscard]] QString period() const { return m_period; }
  void setPeriod(const QString& value);

  [[nodiscard]] int year() const { return m_year; }
  void setYear(int value);

  // "2026" or "All time", for headings and the card.
  [[nodiscard]] QString periodLabel() const { return m_figures.periodLabel; }
  // A sentence naming the window the recorded figures really cover, empty when the period
  // is fully covered by recording.
  [[nodiscard]] QString windowNote() const { return m_figures.windowNote; }
  // Epoch seconds of the earliest session ever recorded, 0 when nothing has been recorded.
  [[nodiscard]] qint64 recordingStartsAt() const { return m_figures.recordingStartsAt; }

  // Recorded time, sessions, days, games, the top game with its share of that time, and the
  // library's own reported totals beside them.
  [[nodiscard]] QVariantMap headline() const { return m_figures.headline; }
  // Recorded time per source (Ryujinx, Steam, ...): entries of name, seconds, sessions,
  // games, share.
  [[nodiscard]] QVariantList bySource() const { return m_figures.bySource; }
  // The same, grouped by console where the source is a dedicated emulator and by source
  // otherwise: entries of name, seconds, share, console.
  [[nodiscard]] QVariantList bySystem() const { return m_figures.bySystem; }
  // 24 entries of hour, seconds; 7 entries of weekday (0 = Monday), seconds.
  [[nodiscard]] QVariantList byHour() const { return m_figures.byHour; }
  [[nodiscard]] QVariantList byWeekday() const { return m_figures.byWeekday; }
  [[nodiscard]] QVariantList topGames() const { return m_figures.topGames; }
  // count, totalSeconds, averageSeconds, longestSeconds, longestTitle, longestStartedAt,
  // overTwoHours, buckets (label, count).
  [[nodiscard]] QVariantMap sessionShape() const { return m_figures.sessionShape; }
  // daysPlayed, longestRun, currentRun, daysOff, firstDay, lastDay.
  [[nodiscard]] QVariantMap streaks() const { return m_figures.streaks; }
  // unlockedInPeriod, unlockedTotal, known, rate, rarest (title, rarity, source, appId,
  // gameTitle), bySource (source, unlocked).
  [[nodiscard]] QVariantMap achievements() const { return m_figures.achievements; }
  // firstTimeGames, oneAndDone, returns, longestGapDays, returnsList (title, gapDays).
  [[nodiscard]] QVariantMap backlog() const { return m_figures.backlog; }
  // games, systems, genres (genre, seconds, share), completions (status, count),
  // topRated (title, rating, ratingCount, source).
  [[nodiscard]] QVariantMap library() const { return m_figures.library; }
  [[nodiscard]] QString error() const { return m_figures.error; }

  // Recomputes everything now, on the calling thread. Called by the tests and when the view
  // first opens.
  Q_INVOKABLE void refresh();

  // How many computations have started, on either thread. For the tests and the scroll trace.
  [[nodiscard]] int computations() const { return m_computations; }
  // True while a background computation is running or queued.
  [[nodiscard]] bool pending() const;

  // Whether a change to these roles can change any figure. An empty list means every role.
  [[nodiscard]] static bool affectsFigures(const QList<int>& roles);
  // Reads the session history named by the snapshot and computes every figure. Touches no
  // shared state, so it runs on any thread.
  [[nodiscard]] static Figures compute(const Snapshot& snapshot);

signals:
  void changed();

private:
  void invalidateLibrary();
  void scheduleRefresh();
  void startBackground();
  void apply(Figures figures);
  [[nodiscard]] Snapshot snapshot();
  [[nodiscard]] QVector<LibraryGame> readLibrary() const;

  UnifiedGameModel* m_games = nullptr;
  QString m_databasePath;
  bool m_active = false;
  bool m_refreshPending = false;
  // The figures no longer match the library or the period, and the next activation or
  // background run must recompute them.
  bool m_dirty = true;
  // No figures have been computed for the current period and year yet.
  bool m_hasFigures = false;
  // A background run has started and its result has not been delivered yet.
  bool m_inFlight = false;
  // A change arrived while a background run was in flight; run again when it lands.
  bool m_rerun = false;
  // The library rows as last read, reused when only the period changes.
  QVector<LibraryGame> m_library;
  bool m_libraryStale = true;
  quint64 m_generation = 0;
  int m_computations = 0;
  QString m_period = QStringLiteral("year");
  int m_year = 0;
  Figures m_figures;
  QFutureWatcher<Figures> m_watcher;
};
