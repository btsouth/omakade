#include "library/GameRoles.h"
#include "library/HomeModel.h"
#include "library/PlayStats.h"
#include "library/UnifiedGameModel.h"
#include "tracking/SessionDatabase.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <algorithm>
#include <functional>

namespace {
using Interval = SessionDatabase::SessionInterval;

qint64 at(int year, int month, int day, int hour, int minute = 0) {
  return QDateTime(QDate(year, month, day), QTime(hour, minute)).toSecsSinceEpoch();
}

struct Database {
  QTemporaryDir dir;
  QString path = dir.filePath("library.sqlite3");
  QString name = QUuid::createUuid().toString();
  QSqlDatabase db;
  Database() {
    if (!SessionDatabase::open(db, path, name))
      qFatal("fixture database failed");
  }
  ~Database() {
    db.close();
    db = {};
    QSqlDatabase::removeDatabase(name);
  }
  void sql(const QString& text) {
    QSqlQuery query(db);
    if (!query.exec(text))
      qFatal("%s", qPrintable(query.lastError().text()));
  }
  // A session as older builds wrote it: a start, an end and a total, nothing in between.
  void legacy(qint64 start, qint64 seconds, qint64 end = -1, const QString& path = "/games/a.nsp") {
    const auto id = SessionDatabase::beginSession(db, path, "Ryujinx", start, 5, 5);
    if (id <= 0 || !SessionDatabase::endSession(db, id, end < 0 ? start + seconds : end, seconds))
      qFatal("fixture session failed");
  }
  // A session the recorder kept intervals for. Returns the session key.
  QString observed(const QVector<Interval>& intervals, const QString& path = "/games/a.nsp") {
    const QString key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qint64 billed = 0;
    for (const auto& interval : intervals)
      billed += interval.billedSeconds;
    const auto id = SessionDatabase::beginSession(db, path, "Ryujinx", intervals.first().wallStart,
                                                  5, 5, key);
    if (id <= 0 || !SessionDatabase::endSession(db, id, intervals.last().wallEnd, billed) ||
        !SessionDatabase::replaceSessionIntervals(db, key, intervals))
      qFatal("fixture observed session failed");
    return key;
  }
};

Interval playing(qint64 start, qint64 end, qint64 billed = -1) {
  return {.wallStart = start, .wallEnd = end, .billedSeconds = billed < 0 ? end - start : billed,
          .kind = QStringLiteral("playing")};
}
Interval paused(qint64 start, qint64 end) {
  return {.wallStart = start, .wallEnd = end, .billedSeconds = 0, .kind = QStringLiteral("paused")};
}

qint64 seconds(const QVariantList& series, int index) {
  return series.at(index).toMap().value("seconds").toLongLong();
}
qint64 total(const QVariantList& series) {
  qint64 sum = 0;
  for (const auto& entry : series)
    sum += entry.toMap().value("seconds").toLongLong();
  return sum;
}

PlayStats::Figures figuresFor(const QString& path, const QString& period, int year,
                              const QVector<PlayStats::LibraryGame>& library = {}) {
  PlayStats::Snapshot snapshot;
  snapshot.databasePath = path;
  snapshot.period = period;
  snapshot.year = year;
  snapshot.library = library;
  snapshot.today = QDate(2026, 12, 31);
  return PlayStats::compute(snapshot);
}

// The allocator Stats used before it read intervals, kept here as the reference the legacy
// path must still match exactly: every session's play spread evenly over its span.
struct Reference {
  qint64 recorded = 0;
  qint64 hours[24] = {};
  qint64 weekdays[7] = {};
  QSet<QDate> days;
};
struct LegacyRow {
  qint64 start = 0;
  qint64 end = 0;
  qint64 seconds = 0;
};
Reference referenceFor(const QVector<LegacyRow>& rows, qint64 from, qint64 to) {
  Reference result;
  for (const LegacyRow& row : rows) {
    const qint64 spanStart = row.start;
    const qint64 spanEnd = qMax(row.start + row.seconds, row.end);
    const auto credited = [&](qint64 boundary) -> qint64 {
      if (boundary <= spanStart) return 0;
      if (boundary >= spanEnd) return row.seconds;
      return static_cast<qint64>(static_cast<long double>(row.seconds) * (boundary - spanStart) /
                                 (spanEnd - spanStart));
    };
    const qint64 clipped = credited(to) - credited(from);
    if (clipped == 0 && !(row.seconds == 0 && row.start >= from && row.start < to))
      continue;
    result.recorded += clipped;
    qint64 cursor = qMax(row.start, from);
    const qint64 end = qMin(to, spanEnd);
    while (cursor < end) {
      const QDateTime moment = QDateTime::fromSecsSinceEpoch(cursor);
      const qint64 nextHour = cursor + 3600 - moment.time().minute() * 60 - moment.time().second();
      const qint64 sliceEnd = qMin(end, nextHour > cursor ? nextHour : cursor + 3600);
      const qint64 slice = credited(sliceEnd) - credited(cursor);
      result.hours[moment.time().hour()] += slice;
      result.weekdays[moment.date().dayOfWeek() - 1] += slice;
      if (slice > 0) result.days.insert(moment.date());
      cursor = sliceEnd;
    }
  }
  return result;
}

// A small library whose rows the tests change one role at a time.
class FixtureSource final : public QAbstractListModel {
public:
  QVector<QHash<int, QVariant>> rows;
  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : static_cast<int>(rows.size());
  }
  QVariant data(const QModelIndex& index, int role) const override {
    return index.isValid() ? rows.at(index.row()).value(role) : QVariant();
  }
  QHash<int, QByteArray> roleNames() const override { return GameRoles::names(); }
  void add(const QString& appId, const QString& path, qint64 lastPlayed) {
    rows.append({{GameRoles::Title, "Game " + appId},
                 {GameRoles::Source, QStringLiteral("Ryujinx")},
                 {GameRoles::System, QStringLiteral("switch")},
                 {GameRoles::AppId, appId},
                 {GameRoles::InstallPath, path},
                 {GameRoles::Installed, true},
                 {GameRoles::LastPlayed, lastPlayed},
                 {GameRoles::PlaytimeSeconds, qint64(600)},
                 {GameRoles::CoverPath, QStringLiteral("/covers/%1-a.png").arg(appId)}});
  }
  void set(int row, int role, const QVariant& value) {
    rows[row][role] = value;
    emit dataChanged(index(row), index(row), {role});
  }
};

// Lets queued background work land without asserting on wall time.
void settle(const std::function<bool()>& idle) {
  for (int round = 0; round < 200 && !idle(); ++round)
    QTest::qWait(5);
  QCoreApplication::processEvents();
}
} // namespace

class StatsAggregationTests : public QObject {
  Q_OBJECT
private slots:
  void intervalsPlacePlayWhereItHappened() {
    Database data;
    // Played 20:00 to 21:00, paused for an hour, played 22:00 to 23:00.
    data.observed({playing(at(2026, 3, 4, 20), at(2026, 3, 4, 21)),
                   paused(at(2026, 3, 4, 21), at(2026, 3, 4, 22)),
                   playing(at(2026, 3, 4, 22), at(2026, 3, 4, 23))});
    const auto figures = figuresFor(data.path, "year", 2026);
    QVERIFY(figures.error.isEmpty());
    QCOMPARE(figures.headline.value("recordedSeconds").toLongLong(), qint64(7200));
    QCOMPARE(seconds(figures.byHour, 20), qint64(3600));
    QCOMPARE(seconds(figures.byHour, 21), qint64(0));
    QCOMPARE(seconds(figures.byHour, 22), qint64(3600));
    QCOMPARE(total(figures.byHour), qint64(7200));
    QVERIFY(!figures.windowNote.contains("estimate"));

    // The same session without intervals is spread over its three hours and says so.
    Database old;
    old.legacy(at(2026, 3, 4, 20), 7200, at(2026, 3, 4, 23));
    const auto estimate = figuresFor(old.path, "year", 2026);
    QCOMPARE(seconds(estimate.byHour, 20), qint64(2400));
    QCOMPARE(seconds(estimate.byHour, 21), qint64(2400));
    QCOMPARE(seconds(estimate.byHour, 22), qint64(2400));
    QVERIFY(estimate.windowNote.contains("estimated"));
  }

  void intervalsSplitPeriodsAndDays() {
    Database data;
    // Fifteen minutes before midnight on New Year's Eve, a pause, then thirty minutes after.
    data.observed({playing(at(2025, 12, 31, 23, 30), at(2025, 12, 31, 23, 45)),
                   paused(at(2025, 12, 31, 23, 45), at(2026, 1, 1, 0, 15)),
                   playing(at(2026, 1, 1, 0, 15), at(2026, 1, 1, 0, 45))});
    const auto before = figuresFor(data.path, "year", 2025);
    const auto after = figuresFor(data.path, "year", 2026);
    // An even spread over the 75 minute span would have credited 2025 with 1080 seconds.
    QCOMPARE(before.headline.value("recordedSeconds").toLongLong(), qint64(900));
    QCOMPARE(after.headline.value("recordedSeconds").toLongLong(), qint64(1800));
    QCOMPARE(total(before.byWeekday), qint64(900));
    QCOMPARE(seconds(before.byWeekday, QDate(2025, 12, 31).dayOfWeek() - 1), qint64(900));
    QCOMPARE(seconds(after.byWeekday, QDate(2026, 1, 1).dayOfWeek() - 1), qint64(1800));
    QCOMPARE(before.headline.value("daysPlayed").toInt(), 1);
    QCOMPARE(before.streaks.value("firstDay").toString(), QString("2025-12-31"));
    QCOMPARE(after.streaks.value("firstDay").toString(), QString("2026-01-01"));
    const auto all = figuresFor(data.path, "all", 2026);
    QCOMPARE(all.headline.value("recordedSeconds").toLongLong(), qint64(2700));
    QCOMPARE(all.streaks.value("longestRun").toInt(), 2);
  }

  void untrustedIntervalsFallBackToTheEstimate() {
    Database data;
    const auto key = data.observed({playing(at(2026, 5, 1, 10), at(2026, 5, 1, 11)),
                                    paused(at(2026, 5, 1, 11), at(2026, 5, 1, 13))});
    // The total moved on without the intervals: they no longer account for the play, so the
    // session is placed as an older one would be rather than by half its evidence.
    data.sql(QStringLiteral("UPDATE play_sessions SET seconds = 5400 WHERE session_key = '%1'").arg(key));
    const auto figures = figuresFor(data.path, "year", 2026);
    QVERIFY(figures.windowNote.contains("estimated"));
    QCOMPARE(total(figures.byHour), qint64(5400));
    QCOMPARE(seconds(figures.byHour, 10), qint64(1800));
    QCOMPARE(seconds(figures.byHour, 12), qint64(1800));

    // A playing interval with far more wall time than play hides a suspend: still an estimate.
    Database slept;
    slept.observed({playing(at(2026, 5, 2, 10), at(2026, 5, 2, 14), 3600)});
    const auto suspend = figuresFor(slept.path, "year", 2026);
    QCOMPARE(seconds(suspend.byHour, 10), qint64(900));
    QCOMPARE(seconds(suspend.byHour, 13), qint64(900));
    QVERIFY(suspend.windowNote.contains("estimated"));
  }

  void legacySessionsMatchTheProportionalAllocator() {
    Database data;
    QVector<LegacyRow> rows;
    QRandomGenerator random(20261108);
    const qint64 first = at(2025, 1, 1, 0);
    const qint64 last = at(2026, 12, 31, 18);
    for (int index = 0; index < 160; ++index) {
      LegacyRow row;
      row.start = first + random.bounded(last - first);
      row.seconds = random.bounded(6 * 3600);
      // About half the sessions span more wall time than play: a pause or a suspend.
      row.end = row.start + row.seconds + (random.bounded(2) == 0 ? 0 : random.bounded(10 * 3600));
      rows.append(row);
    }
    std::sort(rows.begin(), rows.end(), [](const LegacyRow& a, const LegacyRow& b) { return a.start < b.start; });
    for (const LegacyRow& row : rows)
      data.legacy(row.start, row.seconds, row.end);
    const struct {
      const char* period;
      int year;
      qint64 from;
      qint64 to;
    } windows[] = {{"year", 2025, at(2025, 1, 1, 0), at(2026, 1, 1, 0)},
                   {"year", 2026, at(2026, 1, 1, 0), at(2027, 1, 1, 0)},
                   {"all", 2026, 0, 4133980800}};
    for (const auto& window : windows) {
      const auto figures = figuresFor(data.path, window.period, window.year);
      const Reference expected = referenceFor(rows, window.from, window.to);
      QCOMPARE(figures.headline.value("recordedSeconds").toLongLong(), expected.recorded);
      for (int hour = 0; hour < 24; ++hour)
        QCOMPARE(seconds(figures.byHour, hour), expected.hours[hour]);
      for (int day = 0; day < 7; ++day)
        QCOMPARE(seconds(figures.byWeekday, day), expected.weekdays[day]);
      QCOMPARE(figures.headline.value("daysPlayed").toInt(), expected.days.size());
    }
  }

  void artworkChangesRebuildNoStats() {
    Database data;
    data.legacy(at(2026, 4, 1, 12), 900, -1, "/games/a.nsp");
    FixtureSource source;
    source.add("a", "/games/a.nsp", at(2026, 4, 1, 12));
    source.add("b", "/games/b.nsp", 0);
    UnifiedGameModel games(data.path);
    games.addSourceModel(&source);
    PlayStats stats(&games, data.path);
    stats.setYear(2026);
    QCOMPARE(stats.computations(), 0); // nothing is computed while the screen is closed
    stats.setActive(true);
    QCOMPARE(stats.computations(), 1);
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(900));

    source.set(0, GameRoles::CoverPath, QStringLiteral("/covers/a-b.png"));
    source.set(1, GameRoles::HeroPath, QStringLiteral("/covers/b-hero.png"));
    settle([&] { return !stats.pending(); });
    QCOMPARE(stats.computations(), 1);
    QVERIFY(!stats.pending());

    // A change that does move a figure is computed in the background and lands as one update.
    QSignalSpy changed(&stats, &PlayStats::changed);
    source.set(1, GameRoles::Title, QStringLiteral("Renamed"));
    source.set(0, GameRoles::PlaytimeSeconds, qint64(1200));
    QTRY_VERIFY(!changed.isEmpty() && !stats.pending());
    QVERIFY(stats.computations() >= 2);
    const int settled = stats.computations();

    // The background figures are the ones an in-place computation gives for the same state.
    PlayStats reference(&games, data.path);
    reference.setYear(2026);
    reference.refresh();
    QCOMPARE(stats.headline(), reference.headline());
    QCOMPARE(stats.byHour(), reference.byHour());
    QCOMPARE(stats.topGames(), reference.topGames());
    QCOMPARE(stats.library(), reference.library());
    QCOMPARE(stats.windowNote(), reference.windowNote());

    // Closed, a change only marks the figures stale; opening again recomputes in the background
    // while the previous figures stay readable.
    stats.setActive(false);
    source.set(0, GameRoles::LastPlayed, at(2026, 4, 2, 12));
    settle([&] { return !stats.pending(); });
    QCOMPARE(stats.computations(), settled);
    stats.setActive(true);
    QVERIFY(!stats.headline().isEmpty());
    QTRY_COMPARE(stats.computations(), settled + 1);
    QTRY_VERIFY(!stats.pending());
  }

  void periodChangesWaitForTheScreen() {
    Database data;
    data.legacy(at(2025, 6, 1, 12), 600);
    data.legacy(at(2026, 6, 1, 12), 300);
    PlayStats stats(nullptr, data.path);
    stats.setYear(2025);
    stats.setPeriod("all");
    QCOMPARE(stats.computations(), 0);
    stats.setActive(true);
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(900));
    QSignalSpy changed(&stats, &PlayStats::changed);
    stats.setPeriod("year");
    QCOMPARE(stats.period(), QString("year"));
    QTRY_COMPARE(stats.periodLabel(), QString("2025"));
    QTRY_VERIFY(!stats.pending());
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(600));
    QVERIFY(changed.size() >= 2); // the period itself at once, then the figures
  }

  void homeArtworkChangesPatchWithoutRebuilding() {
    Database data;
    FixtureSource source;
    source.add("a", QString(), 1700000300);
    source.add("b", QString(), 1700000200);
    source.add("c", QString(), 0);
    UnifiedGameModel games(data.path);
    games.addSourceModel(&source);
    HomeModel home(&games, data.path);
    home.setActive(true);
    QCOMPARE(home.rebuilds(), 1);
    QCOMPARE(home.recent().size(), 2);
    QCOMPARE(home.recent().first().toMap().value("appId").toString(), QString("a"));
    QVERIFY(home.enqueue("Ryujinx", "", "c"));
    const int afterQueue = home.rebuilds();

    const QVariant before = home.recent().first().toMap().value("coverPath");
    const QString suggested = home.queue().first().toMap().value("subtitle").toString();
    source.set(0, GameRoles::CoverPath, QStringLiteral("/covers/a-new.png"));
    source.set(2, GameRoles::Subtitle, QStringLiteral("Now with a subtitle"));
    const QVariant expected = games.index(0).data(GameRoles::CoverPath);
    QVERIFY(expected != before);
    QTRY_COMPARE(home.recent().first().toMap().value("coverPath"), expected);
    QTRY_COMPARE(home.queue().first().toMap().value("subtitle").toString(), QString("Now with a subtitle"));
    QVERIFY(suggested != "Now with a subtitle");
    // What the shelf added to the row survives the patch.
    QVERIFY(!home.queue().first().toMap().value("queueKey").toString().isEmpty());
    QCOMPARE(home.rebuilds(), afterQueue);
    QVERIFY(!home.pending());

    // A change that moves a game is built in the background and matches an in-place build.
    source.set(1, GameRoles::LastPlayed, qint64(1700000400));
    QTRY_COMPARE(home.recent().first().toMap().value("appId").toString(), QString("b"));
    QTRY_VERIFY(!home.pending());
    QCOMPARE(home.rebuilds(), afterQueue + 1);
    HomeModel reference(&games, data.path);
    reference.refresh();
    QCOMPARE(home.recent(), reference.recent());
    QCOMPARE(home.queue(), reference.queue());
    QCOMPARE(home.suggestions(), reference.suggestions());
    QCOMPARE(home.shortcuts(), reference.shortcuts());
    QCOMPARE(home.gameCount(), reference.gameCount());
  }

  void homeBurstsBuildOnce() {
    Database data;
    FixtureSource source;
    for (int index = 0; index < 40; ++index)
      source.add(QString::number(index), QString(), 1700000000 + index);
    UnifiedGameModel games(data.path);
    games.addSourceModel(&source);
    HomeModel home(&games, data.path);
    home.setActive(true);
    const int start = home.rebuilds();
    // A scan finishing touches many rows in one go; Home builds for the burst, not per row.
    for (int index = 0; index < 40; ++index)
      source.set(index, GameRoles::LastPlayed, qint64(1800000000 - index));
    QTRY_VERIFY(!home.pending());
    QVERIFY(home.rebuilds() - start <= 2);
    QCOMPARE(home.recent().first().toMap().value("appId").toString(), QString("0"));
  }
};

QTEST_GUILESS_MAIN(StatsAggregationTests)
#include "StatsAggregationTests.moc"
