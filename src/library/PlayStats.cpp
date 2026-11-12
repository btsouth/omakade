#include "library/PlayStats.h"

#include "library/ConsoleCatalog.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "library/UserDateFormat.h"
#include "tracking/SessionDisplay.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QScopeGuard>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>
#include <limits>

namespace {
// Calendar distribution is bounded by the supported 1970 through 2100 range.
constexpr qint64 kFarFuture = 4133980800; // 2101-01-01
constexpr QChar kUnitSeparator(0x1f);

// A part of a whole, as a fraction, so the caller formats it.
double shareOf(qint64 part, qint64 whole) {
  if (whole <= 0 || part <= 0)
    return 0.0;
  return static_cast<double>(part) / static_cast<double>(whole);
}

QVariantList sortedBySeconds(const QHash<QString, qint64>& seconds, qint64 total) {
  QVector<QPair<QString, qint64>> entries;
  entries.reserve(seconds.size());
  for (auto it = seconds.cbegin(); it != seconds.cend(); ++it)
    entries.append({it.key(), it.value()});
  std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
    return left.second != right.second ? left.second > right.second : left.first < right.first;
  });
  QVariantList rows;
  rows.reserve(entries.size());
  for (const auto& entry : entries) {
    rows.append(QVariantMap{{QStringLiteral("name"), entry.first},
                            {QStringLiteral("seconds"), entry.second},
                            {QStringLiteral("share"), shareOf(entry.second, total)}});
  }
  return rows;
}

// Achievement sources are named per transport; the library names the same games differently.
QString librarySourceForAchievements(const QString& source) {
  if (source.startsWith(QStringLiteral("steam")))
    return QStringLiteral("Steam");
  return source;
}

// A stretch of wall time and the play credited across it. A session without recorded intervals
// is one piece covering its whole span; a session with them has one piece per playing interval,
// so time it spent paused is credited nowhere.
struct Piece {
  qint64 start = 0;
  qint64 end = 0;
  qint64 seconds = 0;
};

struct RecordedSession {
  qint64 startedAt = 0;
  qint64 endedAt = 0;
  qint64 seconds = 0;
  qint64 spanStart = 0;
  qint64 spanEnd = 0;
  QVector<Piece> pieces;
  bool estimated = false; // some play is spread over wall time longer than the play itself
  QString source;
  QString path;
};

// Cumulative allocation conserves seconds when a piece crosses a calendar boundary.
qint64 pieceCreditedUntil(const Piece& piece, qint64 boundary) {
  if (boundary <= piece.start) return 0;
  if (boundary >= piece.end) return piece.seconds;
  return static_cast<qint64>(static_cast<long double>(piece.seconds) *
                             (boundary - piece.start) / (piece.end - piece.start));
}

qint64 creditedUntil(const RecordedSession& session, qint64 boundary) {
  qint64 credited = 0;
  for (const Piece& piece : session.pieces)
    credited += pieceCreditedUntil(piece, boundary);
  return credited;
}

// A private read-only connection for one computation, closed and removed when it goes out of
// scope. Connections cannot be shared between threads, and a computation may run on any.
class ReadConnection {
public:
  explicit ReadConnection(const QString& path) : m_name(QUuid::createUuid().toString()) {
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
    if (!path.isEmpty()) m_database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    m_database.setDatabaseName(path.isEmpty() ? QStringLiteral(":memory:") : path);
    m_database.open();
  }
  ~ReadConnection() {
    m_database.close();
    m_database = {};
    QSqlDatabase::removeDatabase(m_name);
  }
  ReadConnection(const ReadConnection&) = delete;
  ReadConnection& operator=(const ReadConnection&) = delete;
  QSqlDatabase& database() { return m_database; }

private:
  QString m_name;
  QSqlDatabase m_database;
};

// Recorded play intervals by session key, kept only when they account for exactly the
// session's billed seconds; anything else is left to the even spread.
struct IntervalSet {
  QVector<Piece> pieces;
  qint64 billed = 0;
  bool valid = true;
};
} // namespace

PlayStats::PlayStats(UnifiedGameModel* games, const QString& path, QObject* parent)
    : QObject(parent), m_games(games), m_databasePath(path) {
  m_year = QDate::currentDate().year();
  connect(&m_watcher, &QFutureWatcher<Figures>::finished, this, [this] {
    m_inFlight = false;
    Figures figures = m_watcher.result();
    if (figures.generation == m_generation)
      apply(std::move(figures));
    if (m_rerun) {
      m_rerun = false;
      if (m_active && m_dirty)
        startBackground();
    }
  });
  if (games == nullptr)
    return;
  const auto invalidate = [this] { invalidateLibrary(); };
  connect(games, &QAbstractItemModel::modelReset, this, invalidate);
  connect(games, &QAbstractItemModel::rowsInserted, this, invalidate);
  connect(games, &QAbstractItemModel::rowsRemoved, this, invalidate);
  connect(games, &QAbstractItemModel::layoutChanged, this, invalidate);
  // Covers, artwork, review state and the like change often and never move a figure, so they
  // cost nothing here.
  connect(games, &QAbstractItemModel::dataChanged, this,
          [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
            if (affectsFigures(roles)) invalidateLibrary();
          });
}

PlayStats::~PlayStats() {
  // A computation holds no pointer to this object, but finishing it here keeps its database
  // connection from outliving the application's own teardown.
  m_watcher.waitForFinished();
}

bool PlayStats::affectsFigures(const QList<int>& roles) {
  if (roles.isEmpty())
    return true;
  static const QSet<int> relevant{GameRoles::Title,           GameRoles::Source,
                                  GameRoles::Runner,          GameRoles::System,
                                  GameRoles::AppId,           GameRoles::InstallPath,
                                  GameRoles::Genres,          GameRoles::PlaytimeSeconds,
                                  GameRoles::Hours,           GameRoles::LastPlayed,
                                  GameRoles::CompletionStatus, GameRoles::Rating,
                                  GameRoles::RatingCount,     GameRoles::Linked,
                                  GameRoles::LinkedSources,   GameRoles::IsPortal,
                                  // An achievement sync rewrites the achievements table and
                                  // reports it through these.
                                  GameRoles::Progress,        GameRoles::AchievementsUnlocked,
                                  GameRoles::AchievementsTotal};
  return std::any_of(roles.cbegin(), roles.cend(),
                     [](int role) { return relevant.contains(role); });
}

void PlayStats::setActive(bool value) {
  if (m_active == value)
    return;
  m_active = value;
  // Nothing is computed while the view is closed: the library model changes often, and these
  // figures cost a walk over every row plus every recorded session. The first opening computes
  // in place so the screen and a one-shot card export never see empty figures; after that the
  // previous figures stay on screen while fresh ones are computed in the background. Every
  // opening recomputes, because some figures come from the database or today's date rather
  // than from the library model. The library rows are read again too: a rescan that finds the
  // same games can change their playtime without the unified model saying so.
  if (!value)
    return;
  m_libraryStale = true;
  if (!m_hasFigures) {
    refresh();
    return;
  }
  m_dirty = true;
  scheduleRefresh();
}

void PlayStats::setPeriod(const QString& value) {
  const QString wanted = value == QStringLiteral("all") ? QStringLiteral("all")
                                                        : QStringLiteral("year");
  if (m_period == wanted)
    return;
  m_period = wanted;
  m_hasFigures = false;
  m_dirty = true;
  // A run already in flight describes the old window; drop it rather than apply it.
  ++m_generation;
  emit changed();
  scheduleRefresh();
}

void PlayStats::setYear(int value) {
  if (value < 1970 || value > 2100 || m_year == value)
    return;
  m_year = value;
  m_hasFigures = false;
  m_dirty = true;
  // A run already in flight describes the old window; drop it rather than apply it.
  ++m_generation;
  emit changed();
  scheduleRefresh();
}

bool PlayStats::pending() const { return m_refreshPending || m_inFlight; }

void PlayStats::invalidateLibrary() {
  m_libraryStale = true;
  m_dirty = true;
  scheduleRefresh();
}

void PlayStats::scheduleRefresh() {
  if (!m_active || m_refreshPending)
    return;
  m_refreshPending = true;
  // Coalesce a burst of model signals (a source finishing a scan emits one per row) into one run.
  QTimer::singleShot(0, this, [this] {
    m_refreshPending = false;
    if (m_active && m_dirty)
      startBackground();
  });
}

PlayStats::Snapshot PlayStats::snapshot() {
  if (m_libraryStale) {
    m_library = readLibrary();
    m_libraryStale = false;
  }
  Snapshot snapshot;
  snapshot.library = m_library;
  snapshot.databasePath = m_databasePath;
  snapshot.period = m_period;
  snapshot.year = m_year;
  snapshot.today = QDate::currentDate();
  return snapshot;
}

void PlayStats::startBackground() {
  if (m_inFlight) {
    m_rerun = true;
    return;
  }
  const Snapshot input = snapshot();
  m_dirty = false;
  const quint64 generation = ++m_generation;
  ++m_computations;
  m_inFlight = true;
  m_watcher.setFuture(QtConcurrent::run([input, generation] {
    Figures figures = compute(input);
    figures.generation = generation;
    return figures;
  }));
}

void PlayStats::refresh() {
  QElapsedTimer timer;
  timer.start();
  m_libraryStale = true;
  const Snapshot input = snapshot();
  m_dirty = false;
  // Supersedes any background run still in flight: its figures describe an older state.
  Figures figures = compute(input);
  figures.generation = ++m_generation;
  ++m_computations;
  apply(std::move(figures));
  if (qEnvironmentVariableIsSet("OMAKADE_SCROLL_TRACE"))
    qInfo() << "scroll-trace stats-refresh-ms" << timer.elapsed() << "rows" << input.library.size();
}

void PlayStats::apply(Figures figures) {
  m_figures = std::move(figures);
  m_hasFigures = true;
  emit changed();
}

QVector<PlayStats::LibraryGame> PlayStats::readLibrary() const {
  QVector<LibraryGame> rows;
  if (m_games == nullptr)
    return rows;
  const int count = m_games->rowCount();
  rows.reserve(count);
  for (int row = 0; row < count; ++row) {
    const QModelIndex index = m_games->index(row, 0);
    LibraryGame game;
    game.portal = index.data(GameRoles::IsPortal).toBool();
    game.title = index.data(GameRoles::Title).toString();
    game.source = index.data(GameRoles::Source).toString();
    game.system = index.data(GameRoles::System).toString();
    game.appId = index.data(GameRoles::AppId).toString();
    game.identity = game.source + kUnitSeparator + game.appId;
    const QString installPath = index.data(GameRoles::InstallPath).toString();
    if (!installPath.isEmpty())
      game.paths.append(installPath);
    for (const QVariant& genre : index.data(GameRoles::Genres).toList()) {
      const QString name = genre.toString().trimmed();
      if (!name.isEmpty() && !game.genres.contains(name))
        game.genres.append(name);
    }
    // The library's own number, already reconciled with the recorder by the source models.
    game.playtimeSeconds = index.data(GameRoles::PlaytimeSeconds).toLongLong();
    game.completion = index.data(GameRoles::CompletionStatus).toString();
    game.rating = index.data(GameRoles::Rating).toInt();
    game.ratingCount = index.data(GameRoles::RatingCount).toInt();
    if (index.data(GameRoles::Linked).toBool()) {
      // A linked game records under whichever installation was launched, so every member
      // path belongs to this game. Only linked rows pay for the extra lookup.
      for (const QVariant& member : m_games->installations(row)) {
        const QString memberPath = member.toMap().value(QStringLiteral("installPath")).toString();
        if (!memberPath.isEmpty() && !game.paths.contains(memberPath))
          game.paths.append(memberPath);
      }
    }
    rows.append(game);
  }
  return rows;
}

PlayStats::Figures PlayStats::compute(const Snapshot& snapshot) {
  Figures figures;
  const bool byYear = snapshot.period != QStringLiteral("all");
  const int year = snapshot.year;
  const QDate today = snapshot.today.isValid() ? snapshot.today : QDate::currentDate();
  figures.periodLabel = byYear ? QString::number(year) : QStringLiteral("All time");
  const auto failRead = [&figures] {
    Figures failed;
    failed.periodLabel = figures.periodLabel;
    failed.recordingStartsAt = figures.recordingStartsAt;
    failed.error =
        QStringLiteral("The statistics could not be read. Try again when library storage is available.");
    return failed;
  };

  ReadConnection connection(snapshot.databasePath);
  QSqlDatabase& database = connection.database();
  if (!database.isOpen() || !database.transaction())
    return failRead();
  // A consistent read snapshot prevents a recorder flush between related figures.
  const auto readSnapshot = qScopeGuard([&database] { database.rollback(); });
  const QStringList tables = database.tables();

  const QVector<LibraryGame>& library = snapshot.library;

  // Recorded play intervals, where the recorder kept them. An older database has neither the
  // table nor the session key; every session there is placed by the even spread.
  bool hasIntervals = tables.contains(QStringLiteral("session_intervals"));
  if (hasIntervals) {
    QSqlQuery columns(database);
    bool hasKey = false;
    if (columns.exec(QStringLiteral("PRAGMA table_info(play_sessions)")))
      while (columns.next())
        hasKey = hasKey || columns.value(1).toString() == QStringLiteral("session_key");
    hasIntervals = hasKey;
  }
  QHash<QString, IntervalSet> intervalsByKey;
  if (hasIntervals) {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT session_key, wall_start, wall_end, billed_seconds "
                                   "FROM session_intervals WHERE billed_seconds > 0 "
                                   "ORDER BY session_key, seq")))
      return failRead();
    while (query.next()) {
      IntervalSet& set = intervalsByKey[query.value(0).toString()];
      const qint64 start = query.value(1).toLongLong();
      const qint64 end = query.value(2).toLongLong();
      const qint64 billed = query.value(3).toLongLong();
      if (start <= 0 || start >= kFarFuture || end < start || end > kFarFuture ||
          billed <= 0 || billed > kFarFuture || set.billed > kFarFuture) {
        set.valid = false;
        continue;
      }
      // Billing comes from a monotonic clock, so a wall clock stepped back can leave less wall
      // time than play; the span is widened, as for an aggregate session, rather than dropped.
      set.pieces.append({start, qMax(end, start + billed), billed});
      set.billed += billed;
    }
    if (query.lastError().isValid())
      return failRead();
  }

  // Every session ever, so first-time plays, one-and-done games and returns after a gap are
  // judged against the whole history rather than only the chosen period.
  QVector<RecordedSession> allSessions;
  qint64 allTimeRecordedSeconds = 0;
  {
    QSqlQuery query(database);
    const QString key = hasIntervals ? QStringLiteral("session_key") : QStringLiteral("''");
    if (query.exec(QStringLiteral("SELECT started_at, ended_at, seconds, source, game_path, heartbeat_at, %1 "
                                  "FROM play_sessions ORDER BY started_at, id").arg(key))) {
      while (query.next()) {
        RecordedSession session;
        session.startedAt = query.value(0).toLongLong();
        session.endedAt = query.value(1).toLongLong();
        session.seconds = query.value(2).toLongLong();
        session.source = query.value(3).toString();
        session.path = query.value(4).toString();
        if (session.startedAt < 0 || session.startedAt >= kFarFuture ||
            session.seconds < 0 || session.seconds > kFarFuture - session.startedAt ||
            session.endedAt > kFarFuture || query.value(5).toLongLong() > kFarFuture ||
            allTimeRecordedSeconds > std::numeric_limits<qint64>::max() - session.seconds) {
          return failRead();
        }
        session.spanStart = session.startedAt;
        session.spanEnd = qMax(session.startedAt + session.seconds,
                               session.endedAt > 0 ? session.endedAt : query.value(5).toLongLong());
        const auto intervals = intervalsByKey.constFind(query.value(6).toString());
        if (session.seconds > 0 && intervals != intervalsByKey.cend() && intervals->valid &&
            intervals->billed == session.seconds) {
          session.pieces = intervals->pieces;
          for (const Piece& piece : std::as_const(session.pieces)) {
            session.spanStart = qMin(session.spanStart, piece.start);
            session.spanEnd = qMax(session.spanEnd, piece.end);
          }
        } else {
          session.pieces = {{session.spanStart, session.spanEnd, session.seconds}};
        }
        // A recorded interval can still hide a suspend: the monotonic clock stops while the
        // wall clock runs on. Any piece with more wall time than play is an estimate.
        for (const Piece& piece : std::as_const(session.pieces))
          session.estimated |= piece.end - piece.start > piece.seconds + 2;
        allTimeRecordedSeconds += session.seconds;
        allSessions.append(session);
      }
      if (query.lastError().isValid()) return failRead();
    } else {
      return failRead();
    }
  }
  if (!allSessions.isEmpty())
    figures.recordingStartsAt = allSessions.first().startedAt;

  qint64 from = 0;
  qint64 to = kFarFuture;
  if (byYear) {
    from = QDateTime(QDate(year, 1, 1), QTime(0, 0)).toSecsSinceEpoch();
    to = QDateTime(QDate(year + 1, 1, 1), QTime(0, 0)).toSecsSinceEpoch();
  }

  QVector<RecordedSession> sessions;
  for (const RecordedSession& original : std::as_const(allSessions)) {
    RecordedSession session = original;
    session.seconds = creditedUntil(original, to) - creditedUntil(original, from);
    if (session.seconds == 0 && !(original.seconds == 0 && original.startedAt >= from && original.startedAt < to))
      continue;
    session.startedAt = qMax(from, original.startedAt);
    session.endedAt = qMin(to, original.spanEnd);
    sessions.append(session);
  }

  // Path to game, so a recorded path can be named and given a genre without a second
  // playtime calculation. The library's own rows win; an unknown path keeps a readable name
  // taken from the path itself, using the same helper the recorder uses for the same job.
  QHash<QString, const LibraryGame*> byPath;
  QHash<QString, QString> titleByAppId;
  for (const LibraryGame& game : library) {
    for (const QString& path : game.paths)
      if (!byPath.contains(path))
        byPath.insert(path, &game);
    if (!game.appId.isEmpty())
      titleByAppId.insert(game.source + kUnitSeparator + game.appId, game.title);
  }
  const auto titleForPath = [&byPath](const QString& path) -> QString {
    if (path.isEmpty())
      return QString();
    const LibraryGame* game = byPath.value(path, nullptr);
    if (game != nullptr && !game->title.isEmpty())
      return game->title;
    return SessionDisplay::titleForGamePath(path);
  };

  // A session records only the emulator or launcher that ran it, while the library knows the
  // system (its models name it: switch, ps2, wii). So the system for a session comes from the
  // library's row for that game, then from the row's source, then from the console catalog,
  // and only then falls back to the source name for a launcher.
  const auto systemFor = [&byPath](const RecordedSession& session) {
    const LibraryGame* game = byPath.value(session.path, nullptr);
    const QString raw = game != nullptr ? game->system : QString{};
    if (raw.isEmpty()) return QPair<QString, bool>{session.source, false};
    return QPair<QString, bool>{ConsoleCatalog::displayNameFor(raw), ConsoleCatalog::find(raw) != nullptr};
  };
  const auto identityFor = [&byPath](const RecordedSession& session) {
    const LibraryGame* game = byPath.value(session.path, nullptr);
    return game != nullptr ? game->identity : session.path;
  };

  qint64 recordedSeconds = 0;
  QHash<QString, qint64> secondsByGame;  // keyed by LibraryGame identity
  QHash<QString, QString> titleByGame;
  QHash<QString, qint64> secondsBySource;
  QHash<QString, int> sessionsBySource;
  QHash<QString, QSet<QString>> gamesBySource;
  QHash<QString, qint64> secondsBySystem;
  QSet<QString> consoleSystems;
  QHash<QString, qint64> secondsByGenre;
  qint64 hourSeconds[24] = {};
  qint64 weekdaySeconds[7] = {};
  QSet<QDate> playedDays;
  QSet<QString> playedGames;
  qint64 longestSeconds = 0;
  QString longestTitle;
  qint64 longestStartedAt = 0;
  int overTwoHours = 0;
  int bucketUnder15 = 0;
  int bucketToHour = 0;
  int bucketToThree = 0;
  int bucketOverThree = 0;
  int estimatedSessions = 0;

  for (const RecordedSession& session : std::as_const(sessions)) {
    const qint64 seconds = qMax<qint64>(0, session.seconds);
    recordedSeconds += seconds;
    secondsBySource[session.source] += seconds;
    ++sessionsBySource[session.source];
    if (seconds > 0) gamesBySource[session.source].insert(identityFor(session));
    const LibraryGame* game = byPath.value(session.path, nullptr);
    if (game != nullptr) {
      secondsByGame[game->identity] += seconds;
      titleByGame.insert(game->identity, game->title);
      for (const QString& genre : game->genres)
        secondsByGenre[genre] += seconds;
    } else {
      secondsByGame[session.path] += seconds;
      titleByGame.insert(session.path, titleForPath(session.path));
    }
    const auto system = systemFor(session);
    secondsBySystem[system.first] += seconds;
    if (system.second)
      consoleSystems.insert(system.first);
    if (seconds > 0) {
      // Counted by game rather than by path: a linked game played from two of its installations is
      // one game in the ranking above, and the headline must not disagree with the list under it.
      playedGames.insert(game != nullptr ? game->identity : session.path);
    }
    if (session.estimated)
      ++estimatedSessions;
    if (seconds > longestSeconds) {
      longestSeconds = seconds;
      longestTitle = titleForPath(session.path);
      longestStartedAt = session.startedAt;
    }
    if (seconds > 2 * 3600)
      ++overTwoHours;
    if (seconds < 15 * 60)
      ++bucketUnder15;
    else if (seconds < 3600)
      ++bucketToHour;
    else if (seconds <= 3 * 3600)
      ++bucketToThree;
    else
      ++bucketOverThree;

    // Hour and weekday figures follow the play rather than its start: a session running from
    // 22:00 to 01:00 is two hours of night and one of the small hours. Slices are clipped to
    // the period, so no figure credits time outside the window it claims. A day only counts as
    // played when the recording actually saw play on it.
    for (const Piece& piece : session.pieces) {
      if (piece.seconds <= 0)
        continue;
      qint64 cursor = qMax(piece.start, from);
      const qint64 pieceEnd = qMin(piece.end, to);
      while (cursor < pieceEnd) {
        const QDateTime moment = QDateTime::fromSecsSinceEpoch(cursor);
        // Advance on the epoch timeline, preserving both occurrences of a repeated DST hour.
        const qint64 nextHour = cursor + 3600 - moment.time().minute() * 60 - moment.time().second();
        const qint64 sliceEnd = qMin(pieceEnd, nextHour > cursor ? nextHour : cursor + 3600);
        const qint64 credited = pieceCreditedUntil(piece, sliceEnd) - pieceCreditedUntil(piece, cursor);
        hourSeconds[moment.time().hour()] += credited;
        weekdaySeconds[moment.date().dayOfWeek() - 1] += credited;
        if (credited > 0) playedDays.insert(moment.date());
        cursor = sliceEnd;
      }
    }
  }

  // The library's own totals, counted once and never mixed with recorded time.
  qint64 librarySeconds = 0;
  int libraryGames = 0;
  for (const LibraryGame& game : library) {
    if (game.portal)
      continue;
    ++libraryGames;
    librarySeconds += game.playtimeSeconds;
  }

  // The ranked games, which is what a recap is really made of: the headline carries the first one
  // and the list gives the reader the rest of the shape.
  QVector<QPair<qint64, QString>> rankedGames;
  rankedGames.reserve(secondsByGame.size());
  for (auto it = secondsByGame.cbegin(); it != secondsByGame.cend(); ++it)
    rankedGames.append({it.value(), titleByGame.value(it.key())});
  std::stable_sort(rankedGames.begin(), rankedGames.end(),
                   [](const QPair<qint64, QString>& left, const QPair<qint64, QString>& right) {
                     return left.first != right.first ? left.first > right.first : left.second < right.second;
                   });

  const qint64 topSeconds = rankedGames.isEmpty() ? 0 : rankedGames.first().first;
  const QString topTitle = rankedGames.isEmpty() ? QString() : rankedGames.first().second;

  for (int index = 0; index < rankedGames.size() && index < 5; ++index) {
    figures.topGames.append(QVariantMap{
        {QStringLiteral("title"), rankedGames.at(index).second},
        {QStringLiteral("seconds"), rankedGames.at(index).first},
        {QStringLiteral("share"), shareOf(rankedGames.at(index).first, recordedSeconds)}});
  }

  figures.headline = QVariantMap{
      {QStringLiteral("recordedSeconds"), recordedSeconds},
      {QStringLiteral("recordedSessions"), sessions.size()},
      {QStringLiteral("daysPlayed"), playedDays.size()},
      {QStringLiteral("gamesPlayed"), playedGames.size()},
      {QStringLiteral("allTimeRecordedSeconds"), allTimeRecordedSeconds},
      {QStringLiteral("librarySeconds"), librarySeconds},
      {QStringLiteral("libraryGames"), libraryGames},
      {QStringLiteral("topGameTitle"), topTitle},
      {QStringLiteral("topGameSeconds"), topSeconds},
      {QStringLiteral("topGameShare"), shareOf(topSeconds, recordedSeconds)}};

  figures.bySource = sortedBySeconds(secondsBySource, recordedSeconds);
  for (QVariant& entry : figures.bySource) {
    QVariantMap row = entry.toMap();
    const QString name = row.value(QStringLiteral("name")).toString();
    row.insert(QStringLiteral("sessions"), sessionsBySource.value(name));
    row.insert(QStringLiteral("games"), gamesBySource.value(name).size());
    entry = row;
  }
  figures.bySystem = sortedBySeconds(secondsBySystem, recordedSeconds);
  for (QVariant& entry : figures.bySystem) {
    QVariantMap row = entry.toMap();
    row.insert(QStringLiteral("console"),
               consoleSystems.contains(row.value(QStringLiteral("name")).toString()));
    entry = row;
  }

  for (int hour = 0; hour < 24; ++hour)
    figures.byHour.append(QVariantMap{{QStringLiteral("hour"), hour},
                                      {QStringLiteral("seconds"), hourSeconds[hour]}});
  for (int day = 0; day < 7; ++day)
    figures.byWeekday.append(QVariantMap{{QStringLiteral("weekday"), day},
                                         {QStringLiteral("seconds"), weekdaySeconds[day]}});

  const int sessionCount = sessions.size();
  figures.sessionShape =
      QVariantMap{{QStringLiteral("count"), sessionCount},
                  {QStringLiteral("totalSeconds"), recordedSeconds},
                  {QStringLiteral("averageSeconds"),
                   sessionCount > 0 ? recordedSeconds / sessionCount : qint64(0)},
                  {QStringLiteral("longestSeconds"), longestSeconds},
                  {QStringLiteral("longestTitle"), longestTitle},
                  {QStringLiteral("longestStartedAt"), longestStartedAt},
                  {QStringLiteral("overTwoHours"), overTwoHours},
                  {QStringLiteral("buckets"),
                   QVariantList{
                       QVariantMap{{QStringLiteral("label"), QStringLiteral("Under 15m")},
                                   {QStringLiteral("count"), bucketUnder15}},
                       QVariantMap{{QStringLiteral("label"), QStringLiteral("15m to 1h")},
                                   {QStringLiteral("count"), bucketToHour}},
                       QVariantMap{{QStringLiteral("label"), QStringLiteral("1h to 3h")},
                                   {QStringLiteral("count"), bucketToThree}},
                       QVariantMap{{QStringLiteral("label"), QStringLiteral("Over 3h")},
                                   {QStringLiteral("count"), bucketOverThree}}}}};

  QList<QDate> days = playedDays.values();
  std::sort(days.begin(), days.end());
  int longestRun = 0;
  int trailingRun = 0;
  int run = 0;
  for (int index = 0; index < days.size(); ++index) {
    run = (index > 0 && days.at(index - 1).daysTo(days.at(index)) == 1) ? run + 1 : 1;
    longestRun = qMax(longestRun, run);
  }
  trailingRun = run;
  // A run counts as current while it is still alive: it has to reach today or yesterday,
  // because today may simply not have been played yet.
  const int currentRun = (!days.isEmpty() && days.last() <= today && days.last().daysTo(today) <= 1) ? trailingRun : 0;
  // Days off are counted only across the window recording actually covers. Counting January to
  // September as days off before the recorder existed reports a number that is true of the
  // calendar and false about the person, which is exactly the kind of figure this screen must
  // not show.
  const QDate recordingStart =
      figures.recordingStartsAt > 0 ? QDateTime::fromSecsSinceEpoch(figures.recordingStartsAt).date() : today;
  const QDate windowStart =
      qMax(byYear ? QDate(year, 1, 1) : recordingStart, recordingStart);
  const QDate windowEnd = qMin(byYear ? QDate(year, 12, 31) : today, today);
  const int windowDays = qMax(0, windowStart.daysTo(windowEnd) + 1);
  figures.streaks = QVariantMap{
      {QStringLiteral("daysPlayed"), playedDays.size()},
      {QStringLiteral("longestRun"), longestRun},
      {QStringLiteral("currentRun"), currentRun},
      {QStringLiteral("daysOff"), qMax(0, windowDays - playedDays.size())},
      {QStringLiteral("firstDay"), days.isEmpty() ? QString() : days.first().toString(Qt::ISODate)},
      {QStringLiteral("lastDay"), days.isEmpty() ? QString() : days.last().toString(Qt::ISODate)}};

  // Achievements: the only dated record of progress, and they cover Steam and
  // RetroAchievements alike, so emulator play counts here too.
  if (tables.contains(QStringLiteral("achievements"))) {
    qint64 known = 0;
    qint64 unlockedTotal = 0;
    QSqlQuery query(database);
    if (query.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(unlocked), 0) FROM achievements")) &&
        query.next()) {
      known = query.value(0).toLongLong();
      unlockedTotal = query.value(1).toLongLong();
    } else { return failRead(); }
    QSqlQuery inPeriod(database);
    inPeriod.prepare(QStringLiteral("SELECT COUNT(*) FROM achievements WHERE unlocked = 1 "
                                    "AND unlock_time >= ? AND unlock_time < ?"));
    inPeriod.addBindValue(from);
    inPeriod.addBindValue(to);
    if (!inPeriod.exec() || !inPeriod.next()) return failRead();
    const qint64 unlockedInPeriod = inPeriod.value(0).toLongLong();
    QVariantList bySourceRows;
    QSqlQuery grouped(database);
    grouped.prepare(QStringLiteral("SELECT source, COUNT(*) FROM achievements WHERE unlocked = 1 "
                                   "AND unlock_time >= ? AND unlock_time < ? "
                                   "GROUP BY source ORDER BY COUNT(*) DESC"));
    grouped.addBindValue(from);
    grouped.addBindValue(to);
    if (grouped.exec()) {
      while (grouped.next())
        bySourceRows.append(QVariantMap{{QStringLiteral("source"), grouped.value(0).toString()},
                                        {QStringLiteral("unlocked"),
                                         grouped.value(1).toLongLong()}});
      if (grouped.lastError().isValid()) return failRead();
    } else { return failRead(); }
    QVariantMap rarest;
    QSqlQuery best(database);
    best.prepare(QStringLiteral("SELECT title, rarity, source, app_id FROM achievements "
                                "WHERE unlocked = 1 AND unlock_time >= ? AND unlock_time < ? "
                                "AND rarity > 0 ORDER BY rarity ASC LIMIT 1"));
    best.addBindValue(from);
    best.addBindValue(to);
    if (!best.exec()) return failRead();
    if (best.next()) {
      const QString source = best.value(2).toString();
      const QString appId = best.value(3).toString();
      rarest = QVariantMap{
          {QStringLiteral("title"), best.value(0).toString()},
          {QStringLiteral("rarity"), best.value(1).toDouble()},
          {QStringLiteral("source"), source},
          {QStringLiteral("appId"), appId},
          {QStringLiteral("basis"), QStringLiteral("rarity")},
          {QStringLiteral("gameTitle"),
           titleByAppId.value(librarySourceForAchievements(source) + kUnitSeparator + appId)}};
    } else {
      // Not every source publishes a rarity: RetroAchievements rows carry none at all, and
      // filtering them out made a period with unlocks report "None yet". Fall back to the newest
      // unlock and say so, rather than claiming there was nothing.
      QSqlQuery recent(database);
      recent.prepare(QStringLiteral("SELECT title, source, app_id FROM achievements "
                                    "WHERE unlocked = 1 AND unlock_time >= ? AND unlock_time < ? "
                                    "ORDER BY unlock_time DESC LIMIT 1"));
      recent.addBindValue(from);
      recent.addBindValue(to);
      if (!recent.exec()) return failRead();
      if (recent.next()) {
        const QString source = recent.value(1).toString();
        const QString appId = recent.value(2).toString();
        rarest = QVariantMap{
            {QStringLiteral("title"), recent.value(0).toString()},
            {QStringLiteral("rarity"), 0.0},
            {QStringLiteral("source"), source},
            {QStringLiteral("appId"), appId},
            {QStringLiteral("basis"), QStringLiteral("recent")},
            {QStringLiteral("gameTitle"),
             titleByAppId.value(librarySourceForAchievements(source) + kUnitSeparator + appId)}};
      }
    }
    figures.achievements =
        QVariantMap{{QStringLiteral("available"), true},
                    {QStringLiteral("unlockedInPeriod"), unlockedInPeriod},
                    {QStringLiteral("unlockedTotal"), unlockedTotal},
                    {QStringLiteral("known"), known},
                    {QStringLiteral("rate"), shareOf(unlockedTotal, known)},
                    {QStringLiteral("rarest"), rarest},
                    {QStringLiteral("bySource"), bySourceRows}};
  }

  // Backlog behaviour, judged against the whole history.
  {
    QHash<QString, int> lifetimeSessions;
    QHash<QString, qint64> firstStart;
    QHash<QString, qint64> previousEnd;
    for (const RecordedSession& session : std::as_const(allSessions)) {
      if (session.seconds <= 0) continue;
      const QString identity = identityFor(session);
      ++lifetimeSessions[identity];
      if (!firstStart.contains(identity))
        firstStart.insert(identity, session.startedAt);
    }
    QSet<QString> firstTimeIdentities;
    int returns = 0;
    qint64 longestGapDays = 0;
    QVector<QPair<qint64, QString>> returnGaps;
    for (const RecordedSession& session : std::as_const(allSessions)) {
      if (session.seconds <= 0) continue;
      const QString identity = identityFor(session);
      const qint64 end = session.endedAt > 0
                             ? session.endedAt
                             : session.startedAt + qMax<qint64>(0, session.seconds);
      const auto previous = previousEnd.constFind(identity);
      // Read the previous end before inserting: an insert can rehash and invalidate the
      // iterator, and reading through it afterwards is how this walked into freed memory.
      const qint64 previousValue = previous == previousEnd.cend() ? 0 : previous.value();
      const bool inPeriod = session.startedAt >= from && session.startedAt < to;
      if (previousValue > 0 && session.startedAt > previousValue) {
        const qint64 gapDays = (session.startedAt - previousValue) / 86400;
        if (inPeriod) {
          longestGapDays = qMax(longestGapDays, gapDays);
          if (gapDays >= 30) {
            ++returns;
            returnGaps.append({gapDays, titleForPath(session.path)});
          }
        }
      }
      previousEnd.insert(identity, qMax(end, previousValue));
      if (inPeriod && firstStart.value(identity) == session.startedAt)
        firstTimeIdentities.insert(identity);
    }
    int oneAndDone = 0;
    for (auto it = lifetimeSessions.cbegin(); it != lifetimeSessions.cend(); ++it) {
      if (it.value() != 1)
        continue;
      const qint64 started = firstStart.value(it.key());
      if (started >= from && started < to)
        ++oneAndDone;
    }
    std::sort(returnGaps.begin(), returnGaps.end(),
              [](const auto& left, const auto& right) { return left.first > right.first; });
    QVariantList returnsList;
    for (int index = 0; index < returnGaps.size() && index < 3; ++index)
      returnsList.append(QVariantMap{{QStringLiteral("gapDays"), returnGaps.at(index).first},
                                     {QStringLiteral("title"), returnGaps.at(index).second}});
    figures.backlog = QVariantMap{{QStringLiteral("firstTimeGames"), firstTimeIdentities.size()},
                                  {QStringLiteral("oneAndDone"), oneAndDone},
                                  {QStringLiteral("returns"), returns},
                                  {QStringLiteral("longestGapDays"), longestGapDays},
                                  {QStringLiteral("returnsList"), returnsList}};
  }

  // The library in numbers. Completion is a current state with no date on it, so it is
  // reported as such and never as "finished this period".
  {
    QSet<QString> systems;
    QHash<QString, int> completions;
    struct Rated {
      QString title;
      QString source;
      int rating = 0;
      int ratingCount = 0;
    };
    QVector<Rated> rated;
    for (const LibraryGame& game : library) {
      if (game.portal)
        continue;
      if (!game.system.isEmpty())
        systems.insert(game.system);
      if (!game.completion.isEmpty())
        completions[game.completion] += 1;
      if (game.rating > 0)
        rated.append({game.title, game.source, game.rating, game.ratingCount});
    }
    std::sort(rated.begin(), rated.end(), [](const Rated& left, const Rated& right) {
      if (left.rating != right.rating)
        return left.rating > right.rating;
      return left.ratingCount > right.ratingCount;
    });
    QVariantList topRated;
    for (int index = 0; index < rated.size() && index < 5; ++index)
      topRated.append(QVariantMap{{QStringLiteral("title"), rated.at(index).title},
                                  {QStringLiteral("source"), rated.at(index).source},
                                  {QStringLiteral("rating"), rated.at(index).rating},
                                  {QStringLiteral("ratingCount"), rated.at(index).ratingCount}});
    QVariantList completionRows;
    for (auto it = completions.cbegin(); it != completions.cend(); ++it)
      completionRows.append(QVariantMap{{QStringLiteral("status"), it.key()},
                                        {QStringLiteral("count"), it.value()}});
    figures.library =
        QVariantMap{{QStringLiteral("games"), libraryGames},
                    {QStringLiteral("systems"), systems.size()},
                    {QStringLiteral("genres"), sortedBySeconds(secondsByGenre, recordedSeconds)},
                    {QStringLiteral("completions"), completionRows},
                    {QStringLiteral("topRated"), topRated}};
  }

  // What the recorded figures really cover, so neither the screen nor the card can imply the
  // whole period was watched.
  if (allSessions.isEmpty()) {
    figures.windowNote = QStringLiteral("Nothing has been recorded yet.");
  } else {
    const QDate first = QDateTime::fromSecsSinceEpoch(figures.recordingStartsAt).date();
    if (!byYear) {
      figures.windowNote = QStringLiteral("Recorded since %1.").arg(UserDateFormat::format(first));
    } else if (figures.recordingStartsAt >= to) {
      // The period ends before recording began: say so rather than borrowing the nearest
      // window's figures, which is the failure this whole note exists to prevent.
      figures.windowNote = QStringLiteral("Nothing was recorded in %1; recording starts %2.")
                               .arg(QString::number(year), UserDateFormat::format(first));
    } else if (figures.recordingStartsAt >= from) {
      figures.windowNote = QStringLiteral("Recording starts %1, so the recorded figures cover that "
                                          "date onward rather than the whole of %2.")
                               .arg(UserDateFormat::format(first), QString::number(year));
    }
  }

  if (estimatedSessions > 0) {
    if (!figures.windowNote.isEmpty()) figures.windowNote += QLatin1Char(' ');
    if (estimatedSessions == sessionCount)
      figures.windowNote += QStringLiteral(
          "Hours and days are estimated: these sessions were recorded before Omakade kept play "
          "intervals, or span a suspend.");
    else
      figures.windowNote += QStringLiteral(
          "Hours and days for %1 of %2 sessions are estimated: they were recorded before Omakade "
          "kept play intervals, or span a suspend.")
                                .arg(estimatedSessions)
                                .arg(sessionCount);
  }
  return figures;
}
