#include "library/PlayStats.h"

#include "library/ConsoleCatalog.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "tracking/SessionDisplay.h"

#include <QDateTime>
#include <QSet>
#include <QSqlQuery>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {
// The distributions walk a session hour by hour. A corrupted row with an implausible total
// must not turn that walk into a long loop, so one session contributes at most a month of
// buckets; its recorded seconds still count in full in the totals.
constexpr int kMaxDistributionSteps = 24 * 31;
constexpr qint64 kFarFuture = 4102444800; // 2100-01-01, the open upper bound for all time
constexpr QChar kUnitSeparator(0x1f);

// A part of a whole, as a fraction, so the caller formats it.
double shareOf(qint64 part, qint64 whole) {
  if (whole <= 0 || part <= 0)
    return 0.0;
  return static_cast<double>(part) / static_cast<double>(whole);
}

QString readableDate(const QDate& date) {
  return date.toString(QStringLiteral("d MMMM yyyy"));
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
} // namespace

PlayStats::PlayStats(UnifiedGameModel* games, const QString& path, QObject* parent)
    : QObject(parent), m_games(games), m_connection(QUuid::createUuid().toString()) {
  m_year = QDate::currentDate().year();
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
  m_database.setDatabaseName(path.isEmpty() ? QStringLiteral(":memory:") : path);
  if (!m_database.open())
    m_error = QStringLiteral("Could not read the library database.");
  else
    m_valid = true;
  if (games == nullptr)
    return;
  const auto invalidate = [this] { scheduleRefresh(); };
  connect(games, &QAbstractItemModel::modelReset, this, invalidate);
  connect(games, &QAbstractItemModel::rowsInserted, this, invalidate);
  connect(games, &QAbstractItemModel::rowsRemoved, this, invalidate);
  connect(games, &QAbstractItemModel::layoutChanged, this, invalidate);
  connect(games, &QAbstractItemModel::dataChanged, this, invalidate);
}

PlayStats::~PlayStats() {
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connection);
}

void PlayStats::setActive(bool value) {
  if (m_active == value)
    return;
  m_active = value;
  // Nothing is computed while the view is closed: the library model changes often, and these
  // figures cost a walk over every row plus every recorded session.
  if (value)
    refresh();
}

void PlayStats::setPeriod(const QString& value) {
  const QString wanted = value == QStringLiteral("all") ? QStringLiteral("all")
                                                        : QStringLiteral("year");
  if (m_period == wanted)
    return;
  m_period = wanted;
  refresh();
}

void PlayStats::setYear(int value) {
  if (value < 1970 || value > 2100 || m_year == value)
    return;
  m_year = value;
  refresh();
}

void PlayStats::scheduleRefresh() {
  if (!m_active || m_refreshPending)
    return;
  m_refreshPending = true;
  QTimer::singleShot(0, this, [this] {
    m_refreshPending = false;
    if (m_active)
      recompute();
  });
}

void PlayStats::refresh() { recompute(); }

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

void PlayStats::recompute() {
  // Reset first, so a period with nothing in it cannot keep the previous figures on screen.
  m_periodLabel = m_period == QStringLiteral("all") ? QStringLiteral("All time")
                                                    : QString::number(m_year);
  m_headline.clear();
  m_bySource.clear();
  m_bySystem.clear();
  m_byHour.clear();
  m_byWeekday.clear();
  m_sessionShape.clear();
  m_streaks.clear();
  m_achievements.clear();
  m_backlog.clear();
  m_library.clear();
  m_windowNote.clear();
  m_recordingStartsAt = 0;
  if (!m_valid) {
    emit changed();
    return;
  }

  const QVector<LibraryGame> library = readLibrary();

  // Every session ever, so first-time plays, one-and-done games and returns after a gap are
  // judged against the whole history rather than only the chosen period.
  QVector<RecordedSession> allSessions;
  qint64 allTimeRecordedSeconds = 0;
  {
    QSqlQuery query(m_database);
    if (query.exec(QStringLiteral("SELECT started_at, ended_at, seconds, source, game_path "
                                  "FROM play_sessions ORDER BY started_at, id"))) {
      while (query.next()) {
        RecordedSession session;
        session.startedAt = query.value(0).toLongLong();
        session.endedAt = query.value(1).toLongLong();
        session.seconds = query.value(2).toLongLong();
        session.source = query.value(3).toString();
        session.path = query.value(4).toString();
        allTimeRecordedSeconds += qMax<qint64>(0, session.seconds);
        allSessions.append(session);
      }
    }
  }
  if (!allSessions.isEmpty())
    m_recordingStartsAt = allSessions.first().startedAt;

  qint64 from = 0;
  qint64 to = kFarFuture;
  const bool byYear = m_period != QStringLiteral("all");
  if (byYear) {
    from = QDateTime(QDate(m_year, 1, 1), QTime(0, 0)).toSecsSinceEpoch();
    to = QDateTime(QDate(m_year + 1, 1, 1), QTime(0, 0)).toSecsSinceEpoch();
  }

  QVector<RecordedSession> sessions;
  for (const RecordedSession& session : std::as_const(allSessions))
    if (session.startedAt >= from && session.startedAt < to)
      sessions.append(session);

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
  QHash<QString, QString> systemBySource;
  for (const LibraryGame& game : library)
    if (!game.source.isEmpty() && !game.system.isEmpty() && !systemBySource.contains(game.source))
      systemBySource.insert(game.source, game.system);
  const auto systemFor = [&byPath, &systemBySource](const RecordedSession& session) {
    const LibraryGame* game = byPath.value(session.path, nullptr);
    const QString raw = game != nullptr && !game->system.isEmpty()
                            ? game->system
                            : systemBySource.value(session.source);
    if (raw.isEmpty())
      return QPair<QString, bool>{session.source, false};
    return QPair<QString, bool>{ConsoleCatalog::displayNameFor(raw),
                                ConsoleCatalog::find(raw) != nullptr};
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
  QSet<QString> playedPaths;
  qint64 longestSeconds = 0;
  QString longestTitle;
  qint64 longestStartedAt = 0;
  int overTwoHours = 0;
  int bucketUnder15 = 0;
  int bucketToHour = 0;
  int bucketToThree = 0;
  int bucketOverThree = 0;

  for (const RecordedSession& session : std::as_const(sessions)) {
    const qint64 seconds = qMax<qint64>(0, session.seconds);
    recordedSeconds += seconds;
    secondsBySource[session.source] += seconds;
    ++sessionsBySource[session.source];
    gamesBySource[session.source].insert(session.path);
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
    if (seconds > 0)
      playedPaths.insert(session.path);

    const QDateTime started = QDateTime::fromSecsSinceEpoch(session.startedAt);
    playedDays.insert(started.date());
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
    // the period, so no figure credits time outside the window it claims.
    qint64 cursor = qMax(session.startedAt, from);
    const qint64 spanEnd = qMin(session.startedAt + seconds, to);
    int steps = 0;
    while (cursor < spanEnd && steps++ < kMaxDistributionSteps) {
      const QDateTime moment = QDateTime::fromSecsSinceEpoch(cursor);
      const qint64 nextHour =
          QDateTime(moment.date(), QTime(moment.time().hour(), 0)).addSecs(3600).toSecsSinceEpoch();
      const qint64 sliceEnd = qMin(spanEnd, nextHour > cursor ? nextHour : cursor + 3600);
      hourSeconds[moment.time().hour()] += qMax<qint64>(0, sliceEnd - cursor);
      weekdaySeconds[moment.date().dayOfWeek() - 1] += qMax<qint64>(0, sliceEnd - cursor);
      cursor = sliceEnd;
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

  qint64 topSeconds = 0;
  QString topTitle;
  for (auto it = secondsByGame.cbegin(); it != secondsByGame.cend(); ++it) {
    if (it.value() > topSeconds) {
      topSeconds = it.value();
      topTitle = titleByGame.value(it.key());
    }
  }

  m_headline = QVariantMap{
      {QStringLiteral("recordedSeconds"), recordedSeconds},
      {QStringLiteral("recordedSessions"), sessions.size()},
      {QStringLiteral("daysPlayed"), playedDays.size()},
      {QStringLiteral("gamesPlayed"), playedPaths.size()},
      {QStringLiteral("allTimeRecordedSeconds"), allTimeRecordedSeconds},
      {QStringLiteral("librarySeconds"), librarySeconds},
      {QStringLiteral("libraryGames"), libraryGames},
      {QStringLiteral("topGameTitle"), topTitle},
      {QStringLiteral("topGameSeconds"), topSeconds},
      {QStringLiteral("topGameShare"), shareOf(topSeconds, recordedSeconds)}};

  m_bySource = sortedBySeconds(secondsBySource, recordedSeconds);
  for (QVariant& entry : m_bySource) {
    QVariantMap row = entry.toMap();
    const QString name = row.value(QStringLiteral("name")).toString();
    row.insert(QStringLiteral("sessions"), sessionsBySource.value(name));
    row.insert(QStringLiteral("games"), gamesBySource.value(name).size());
    entry = row;
  }
  m_bySystem = sortedBySeconds(secondsBySystem, recordedSeconds);
  for (QVariant& entry : m_bySystem) {
    QVariantMap row = entry.toMap();
    row.insert(QStringLiteral("console"),
               consoleSystems.contains(row.value(QStringLiteral("name")).toString()));
    entry = row;
  }

  for (int hour = 0; hour < 24; ++hour)
    m_byHour.append(QVariantMap{{QStringLiteral("hour"), hour},
                                {QStringLiteral("seconds"), hourSeconds[hour]}});
  for (int day = 0; day < 7; ++day)
    m_byWeekday.append(QVariantMap{{QStringLiteral("weekday"), day},
                                   {QStringLiteral("seconds"), weekdaySeconds[day]}});

  const int sessionCount = sessions.size();
  m_sessionShape =
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
  const QDate today = QDate::currentDate();
  // A run counts as current while it is still alive: it has to reach today or yesterday,
  // because today may simply not have been played yet.
  const int currentRun = (!days.isEmpty() && days.last().daysTo(today) <= 1) ? trailingRun : 0;
  const QDate windowStart =
      byYear ? QDate(m_year, 1, 1)
             : (m_recordingStartsAt > 0
                    ? QDateTime::fromSecsSinceEpoch(m_recordingStartsAt).date()
                    : today);
  const QDate windowEnd = qMin(byYear ? QDate(m_year, 12, 31) : today, today);
  const int windowDays = qMax(0, windowStart.daysTo(windowEnd) + 1);
  m_streaks = QVariantMap{
      {QStringLiteral("daysPlayed"), playedDays.size()},
      {QStringLiteral("longestRun"), longestRun},
      {QStringLiteral("currentRun"), currentRun},
      {QStringLiteral("daysOff"), qMax(0, windowDays - playedDays.size())},
      {QStringLiteral("firstDay"), days.isEmpty() ? QString() : days.first().toString(Qt::ISODate)},
      {QStringLiteral("lastDay"), days.isEmpty() ? QString() : days.last().toString(Qt::ISODate)}};

  // Achievements: the only dated record of progress, and they cover Steam and
  // RetroAchievements alike, so emulator play counts here too.
  {
    qint64 known = 0;
    qint64 unlockedTotal = 0;
    QSqlQuery query(m_database);
    if (query.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(unlocked), 0) FROM achievements")) &&
        query.next()) {
      known = query.value(0).toLongLong();
      unlockedTotal = query.value(1).toLongLong();
    }
    qint64 unlockedInPeriod = 0;
    QSqlQuery inPeriod(m_database);
    inPeriod.prepare(QStringLiteral("SELECT COUNT(*) FROM achievements WHERE unlocked = 1 "
                                    "AND unlock_time >= ? AND unlock_time < ?"));
    inPeriod.addBindValue(from);
    inPeriod.addBindValue(to);
    if (inPeriod.exec() && inPeriod.next())
      unlockedInPeriod = inPeriod.value(0).toLongLong();
    QVariantList bySourceRows;
    QSqlQuery grouped(m_database);
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
    }
    QVariantMap rarest;
    QSqlQuery best(m_database);
    best.prepare(QStringLiteral("SELECT title, rarity, source, app_id FROM achievements "
                                "WHERE unlocked = 1 AND unlock_time >= ? AND unlock_time < ? "
                                "AND rarity > 0 ORDER BY rarity ASC LIMIT 1"));
    best.addBindValue(from);
    best.addBindValue(to);
    if (best.exec() && best.next()) {
      const QString source = best.value(2).toString();
      const QString appId = best.value(3).toString();
      rarest = QVariantMap{
          {QStringLiteral("title"), best.value(0).toString()},
          {QStringLiteral("rarity"), best.value(1).toDouble()},
          {QStringLiteral("source"), source},
          {QStringLiteral("appId"), appId},
          {QStringLiteral("gameTitle"),
           titleByAppId.value(librarySourceForAchievements(source) + kUnitSeparator + appId)}};
    }
    m_achievements =
        QVariantMap{{QStringLiteral("unlockedInPeriod"), unlockedInPeriod},
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
      ++lifetimeSessions[session.path];
      if (!firstStart.contains(session.path))
        firstStart.insert(session.path, session.startedAt);
    }
    int firstTimeGames = 0;
    int returns = 0;
    qint64 longestGapDays = 0;
    QVector<QPair<qint64, QString>> returnGaps;
    for (const RecordedSession& session : std::as_const(allSessions)) {
      const qint64 end = session.endedAt > 0
                             ? session.endedAt
                             : session.startedAt + qMax<qint64>(0, session.seconds);
      const auto previous = previousEnd.constFind(session.path);
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
      previousEnd.insert(session.path, qMax(end, previousValue));
      if (inPeriod && firstStart.value(session.path) == session.startedAt)
        ++firstTimeGames;
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
    m_backlog = QVariantMap{{QStringLiteral("firstTimeGames"), firstTimeGames},
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
    m_library =
        QVariantMap{{QStringLiteral("games"), libraryGames},
                    {QStringLiteral("systems"), systems.size()},
                    {QStringLiteral("genres"), sortedBySeconds(secondsByGenre, recordedSeconds)},
                    {QStringLiteral("completions"), completionRows},
                    {QStringLiteral("topRated"), topRated}};
  }

  // What the recorded figures really cover, so neither the screen nor the card can imply the
  // whole period was watched.
  if (allSessions.isEmpty()) {
    m_windowNote = QStringLiteral("Nothing has been recorded yet.");
  } else {
    const QDate first = QDateTime::fromSecsSinceEpoch(m_recordingStartsAt).date();
    if (!byYear) {
      m_windowNote = QStringLiteral("Recorded since %1.").arg(readableDate(first));
    } else if (m_recordingStartsAt >= to) {
      // The period ends before recording began: say so rather than borrowing the nearest
      // window's figures, which is the failure this whole note exists to prevent.
      m_windowNote = QStringLiteral("Nothing was recorded in %1; recording starts %2.")
                         .arg(QString::number(m_year), readableDate(first));
    } else if (m_recordingStartsAt >= from) {
      m_windowNote = QStringLiteral("Recording starts %1, so the recorded figures cover that "
                                    "date onward rather than the whole of %2.")
                         .arg(readableDate(first), QString::number(m_year));
    }
  }

  emit changed();
}
