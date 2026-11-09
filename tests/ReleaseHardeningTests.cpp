#include "app/AppSettings.h"
#include "backup/BackupDatabase.h"
#include "library/GameRoles.h"
#include "library/PlayStats.h"
#include "library/UnifiedGameModel.h"
#include "tracking/DiscordPresence.h"
#include "tracking/GameStop.h"
#include "tracking/PlaySessionStore.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionJournal.h"
#include "tracking/SessionRecorder.h"
#include "tracking/SessionTitleIndex.h"
#include <QJsonArray>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUuid>
#include <QtEndian>
#include <atomic>
#include <memory>

namespace {
class LinkedSource final : public QAbstractListModel {
public:
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 2; }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid())
      return {};
    switch (role) {
    case GameRoles::Title:
      return QStringLiteral("Linked Game");
    case GameRoles::Source:
      return QStringLiteral("RetroArch");
    case GameRoles::System:
      return QStringLiteral("snes");
    case GameRoles::AppId:
      return QString::number(index.row());
    case GameRoles::InstallPath:
      return QStringLiteral("/games/copy%1.sfc").arg(index.row());
    case GameRoles::Runner:
      return QString{};
    default:
      return {};
    }
  }
  QHash<int, QByteArray> roleNames() const override { return GameRoles::names(); }
};

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
  qint64 add(qint64 start, qint64 seconds, const QString& path = "/games/test.nsp",
             qint64 end = -1) {
    const auto id = SessionDatabase::beginSession(db, path, "Ryujinx", start, 5, 5);
    if (id <= 0 || !SessionDatabase::endSession(db, id, end < 0 ? start + seconds : end, seconds))
      qFatal("fixture session failed");
    return id;
  }
};
qint64 timestamp(int year, int month, int day, int hour, int minute = 0) {
  return QDateTime(QDate(year, month, day), QTime(hour, minute)).toSecsSinceEpoch();
}
qint64 hourTotal(const PlayStats& stats) {
  qint64 result = 0;
  for (const auto& entry : stats.byHour())
    result += entry.toMap().value("seconds").toLongLong();
  return result;
}
void refresh(PlaySessionStore& store) {
  store.setEnabled(false);
  store.setEnabled(true);
}
} // namespace

class ReleaseHardeningTests : public QObject {
  Q_OBJECT
private slots:
  void recoveryAcrossFailedPolls_data() {
    QTest::addColumn<int>("polls");
    QTest::newRow("one") << 1;
    QTest::newRow("checkpointed") << 2;
  }
  void recoveryAcrossFailedPolls() {
    QFETCH(int, polls);
    Database data;
    ProcessSnapshot self;
    for (const auto& process : ProcFs::listProcesses())
      if (process.pid == QCoreApplication::applicationPid())
        self = process;
    QVERIFY(self.procStart > 0);
    SessionJournal::Operation op;
    op.key = "original";
    op.gamePath = "/games/recovery.nsp";
    op.source = "Ryujinx";
    op.startedAt = 1000;
    op.seconds = 120;
    op.observedAt = 1120;
    op.pid = self.pid;
    op.procStart = self.procStart;
    op.open = true;
    op.incarnation = SessionDatabase::journalIncarnation(data.db);
    SessionJournal journal(data.path + ".journal");
    QVERIFY(journal.open());
    QVERIFY(journal.append(op));
    data.sql(
        "CREATE TRIGGER deny BEFORE INSERT ON play_sessions BEGIN SELECT RAISE(ABORT,'no'); END");
    qint64 clock = 0;
    SessionRecorder recorder(data.db, [&] { return clock; });
    recorder.recover({}, {}, 2000);
    const SessionMatch match{.pid = self.pid,
                             .procStart = self.procStart,
                             .emulator = "Ryujinx",
                             .gamePath = op.gamePath};
    for (int i = 0; i < polls; ++i) {
      clock += 31000;
      recorder.sync({match}, 2000 + clock / 1000);
    }
    data.sql("DROP TRIGGER deny");
    clock += 31000;
    recorder.sync({match}, 2000 + clock / 1000);
    clock += 31000;
    recorder.sync({}, 2000 + clock / 1000);
    QSqlQuery q(data.db);
    QVERIFY(q.exec("SELECT session_key, seconds, ended_at FROM play_sessions"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), op.key);
    QCOMPARE(q.value(1).toLongLong(), qint64(120 + (polls + 1) * 31));
    QVERIFY(q.value(2).toLongLong() > 0);
    QVERIFY(!q.next());
    QVERIFY(journal.pending(512).isEmpty());
  }

  void newOutageAfterReplace_data() {
    QTest::addColumn<bool>("readableStartup");
    QTest::newRow("immediate-outage") << false;
    QTest::newRow("after-readable-startup") << true;
  }
  void newOutageAfterReplace() {
    QFETCH(bool, readableStartup);
    Database data;
    {
      SessionRecorder initial(data.db, [] { return qint64(0); });
    }
    BackupPayload payload;
    payload.createdAt = "2026-09-19T00:00:00Z";
    payload.library.insert("play_sessions", QJsonArray{});
    payload.library.insert("play_baselines", QJsonArray{});
    QString error;
    QVERIFY2(BackupDatabase::restore(data.path, payload, BackupDatabase::Mode::Replace, &error),
             qPrintable(error));
    if (readableStartup) {
      SessionRecorder initial(data.db, [] { return qint64(0); });
    }
    data.sql("PRAGMA journal_mode=DELETE");
    data.sql("PRAGMA busy_timeout=1");
    const QString connection = QUuid::createUuid().toString();
    {
      auto lock = QSqlDatabase::addDatabase("QSQLITE", connection);
      lock.setDatabaseName(data.path);
      QVERIFY(lock.open());
      QSqlQuery query(lock);
      QVERIFY(query.exec("BEGIN EXCLUSIVE"));
      qint64 clock = 0;
      SessionRecorder recorder(data.db, [&] { return clock; });
      const SessionMatch match{
          .pid = 99999991, .procStart = 10, .emulator = "Ryujinx", .gamePath = "/games/new.nsp"};
      recorder.sync({match}, 3000);
      clock = 60000;
      recorder.sync({}, 3060);
      QVERIFY(query.exec("ROLLBACK"));
      clock = 91000;
      recorder.sync({}, 3091);
      QSqlQuery rows(data.db);
      QVERIFY(rows.exec("SELECT seconds FROM play_sessions"));
      QVERIFY(rows.next());
      QCOMPARE(rows.value(0).toLongLong(), qint64(60));
      QVERIFY(!rows.next());
    }
    QSqlDatabase::removeDatabase(connection);
  }

  void retryUsesNewestDurableCheckpoint() {
    Database data;
    SessionJournal::Operation op;
    op.key = "newest";
    op.gamePath = "/games/newest.nsp";
    op.source = "Ryujinx";
    op.startedAt = 1000;
    op.seconds = 60;
    op.observedAt = 1060;
    op.pid = 99999991;
    op.procStart = 10;
    op.open = true;
    op.incarnation = SessionDatabase::journalIncarnation(data.db);
    SessionJournal journal(data.path + ".journal");
    QVERIFY(journal.open());
    QVERIFY(journal.append(op));
    data.sql(
        "CREATE TRIGGER deny BEFORE INSERT ON play_sessions BEGIN SELECT RAISE(ABORT,'no'); END");
    qint64 clock = 0;
    SessionRecorder recorder(data.db, [&] { return clock; });
    recorder.recover({}, {}, 2000);
    op.seconds = 120;
    op.observedAt = 1120;
    QVERIFY(journal.append(op));
    data.sql("DROP TRIGGER deny");
    clock = 31000;
    recorder.sync({}, 2031);
    auto row = SessionDatabase::sessionByKey(data.db, op.key);
    QCOMPARE(row.seconds, qint64(120));
    QCOMPARE(row.endedAt, qint64(1120));
    QVERIFY(journal.pending(512).isEmpty());
  }

  void oldCheckpointClosesAtNewestObservation() {
    Database data;
    SessionJournal::Operation op;
    op.key = "older";
    op.gamePath = "/games/old.nsp";
    op.source = "Ryujinx";
    op.startedAt = 1000;
    op.seconds = 60;
    op.observedAt = 1060;
    op.pid = 99999991;
    op.procStart = 10;
    op.open = true;
    op.incarnation = SessionDatabase::journalIncarnation(data.db);
    auto id = SessionDatabase::beginSession(data.db, op.gamePath, op.source, 1000, op.pid,
                                            op.procStart, op.key);
    QVERIFY(id > 0);
    QVERIFY(SessionDatabase::updateProgress(data.db, id, 120, 1120));
    SessionJournal journal(data.path + ".journal");
    QVERIFY(journal.open());
    QVERIFY(journal.append(op));
    SessionRecorder recorder(data.db, [] { return qint64(0); });
    recorder.recover({}, {}, 2000);
    auto row = SessionDatabase::sessionByKey(data.db, op.key);
    QCOMPARE(row.seconds, qint64(120));
    QCOMPARE(row.endedAt, qint64(1120));
  }

  void sameStoreCountsPlayAfterDeletion_data() {
    QTest::addColumn<bool>("all");
    QTest::newRow("single") << false;
    QTest::newRow("all") << true;
  }
  void sameStoreCountsPlayAfterDeletion() {
    QFETCH(bool, all);
    Database data;
    const QString path = "/games/test.nsp";
    PlaySessionStore store(data.path);
    store.observeImportedPlaytime(path, 3600);
    data.add(1000, 600);
    refresh(store);
    store.observeImportedPlaytime(path, 4200);
    if (all)
      QCOMPARE(store.deleteHistoryForPaths({path}), 1);
    else
      QVERIFY(store.deleteSession(
          store.historyForPaths({path}).first().toMap().value("sessionKey").toString(), {path}));
    data.add(5000, 900);
    refresh(store);
    store.observeImportedPlaytime(path, 4200);
    QCOMPARE(store.displaySeconds(path, 4200), qint64(5100));
  }
  void failedWatermarkRollsBackDeletion_data() { sameStoreCountsPlayAfterDeletion_data(); }
  void failedWatermarkRollsBackDeletion() {
    QFETCH(bool, all);
    Database data;
    const QString path = "/games/test.nsp";
    PlaySessionStore store(data.path);
    store.observeImportedPlaytime(path, 3600);
    data.add(1000, 600);
    store.observeImportedPlaytime(path, 4200);
    refresh(store);
    const auto key = store.historyForPaths({path}).first().toMap().value("sessionKey").toString();
    data.sql("CREATE TRIGGER refuse_update BEFORE UPDATE OF observed_seconds ON play_baselines "
             "BEGIN SELECT RAISE(FAIL,'refused'); END");
    if (all)
      QCOMPARE(store.deleteHistoryForPaths({path}), -1);
    else
      QVERIFY(!store.deleteSession(key, {path}));
    QCOMPARE(store.historyForPaths({path}).size(), 1);
    QCOMPARE(SessionDatabase::importWatermarksByPath(data.db).value(path).observedSeconds,
             qint64(600));
    data.sql("DROP TRIGGER refuse_update");
    QCOMPARE(store.deleteHistoryForPaths({path}), 1);
  }
  void statisticsFailClosedAndRecover() {
    Database data;
    PlayStats stats(nullptr, data.path);
    stats.refresh();
    QVERIFY(stats.error().isEmpty());
    data.sql("DROP TABLE play_sessions");
    stats.refresh();
    QVERIFY(!stats.error().isEmpty());
    QVERIFY(stats.headline().isEmpty());
    QVERIFY(stats.topGames().isEmpty());
    QVERIFY(SessionDatabase::ensureSchema(data.db));
    data.add(timestamp(2026, 9, 1, 12), 120);
    stats.setYear(2026);
    stats.refresh();
    QVERIFY(stats.error().isEmpty());
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(120));
    data.sql("CREATE TABLE achievements(unlocked INTEGER)");
    stats.refresh();
    QVERIFY(!stats.error().isEmpty());
  }
  void corruptDatabaseIsNotZeroPlay() {
    QTemporaryDir dir;
    const auto path = dir.filePath("broken.sqlite3");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not sqlite");
    file.close();
    PlayStats stats(nullptr, path);
    stats.refresh();
    QVERIFY(!stats.error().isEmpty());
    QVERIFY(stats.headline().isEmpty());
  }
  void calendarBoundariesConservePlay_data() {
    QTest::addColumn<qint64>("start");
    QTest::addColumn<int>("seconds");
    QTest::addColumn<int>("days");
    QTest::newRow("midnight") << timestamp(2026, 9, 1, 23, 30) << 7200 << 2;
    QTest::newRow("long-paused-session") << timestamp(2026, 1, 1, 0) << 40 * 86400 << 40;
    QTest::newRow("leap-day") << timestamp(2024, 2, 28, 23, 30) << 90000 << 3;
    QTest::newRow("spring-DST") << timestamp(2026, 3, 8, 0, 30) << 14400 << 1;
    QTest::newRow("fall-DST") << timestamp(2026, 11, 1, 0, 30) << 14400 << 1;
  }
  void calendarBoundariesConservePlay() {
    QFETCH(qint64, start);
    QFETCH(int, seconds);
    QFETCH(int, days);
    Database data;
    data.add(start, seconds);
    PlayStats stats(nullptr, data.path);
    stats.setPeriod("all");
    stats.refresh();
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(seconds));
    QCOMPARE(hourTotal(stats), qint64(seconds));
    QCOMPARE(stats.headline().value("daysPlayed").toInt(), days);
  }
  void newYearAndPausedSplitsConservePlay() {
    Database data;
    const auto start = timestamp(2025, 12, 31, 23, 30);
    data.add(start, 7200);
    PlayStats stats(nullptr, data.path);
    stats.setYear(2025);
    stats.refresh();
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(1800));
    QCOMPARE(hourTotal(stats), qint64(1800));
    stats.setYear(2026);
    stats.refresh();
    QCOMPARE(stats.headline().value("recordedSeconds").toLongLong(), qint64(5400));
    QCOMPARE(hourTotal(stats), qint64(5400));
    data.sql("DELETE FROM play_sessions");
    data.add(start, 3600, "/games/test.nsp", start + 7200);
    stats.setYear(2025);
    stats.refresh();
    QCOMPARE(hourTotal(stats), qint64(900));
    QVERIFY(stats.windowNote().contains("estimate"));
    stats.setYear(2026);
    stats.refresh();
    QCOMPARE(hourTotal(stats), qint64(2700));
  }
  void titleTokenDetectsEqualLengthChanges() {
    Database data;
    data.sql("CREATE TABLE ryujinx_games(name TEXT,path TEXT)");
    data.sql("INSERT INTO ryujinx_games VALUES('Alpha Game','/games/a.nsp')");
    SessionTitleIndex index;
    QVERIFY(index.refresh(data.db));
    const auto before = SessionTitleIndex::cacheChangeToken(data.db);
    data.sql("UPDATE ryujinx_games SET name='Bravo Game',path='/games/b.nsp'");
    QVERIFY(before != SessionTitleIndex::cacheChangeToken(data.db));
    QVERIFY(index.refresh(data.db));
    QCOMPARE(index.pathForWindowTitle("Bravo Game", "Ryujinx"), QString("/games/b.nsp"));
    QVERIFY(index.pathForWindowTitle("Alpha Game", "Ryujinx").isEmpty());
  }
  void recoveryPreservesActiveStart() {
    Database data;
    for (auto process : ProcFs::listProcesses()) {
      if (process.pid != QCoreApplication::applicationPid())
        continue;
      process.comm = "audit_emulator";
      process.arguments = {"audit_emulator", "/games/test.nsp"};
      ProcessProfileSet profiles;
      profiles.romExtensions = {"nsp"};
      profiles.emulators.append({"Audit", {"audit_emulator"}, {}});
      QVERIFY(SessionDatabase::beginSession(data.db, "/games/test.nsp", "Audit", 12345, process.pid,
                                            process.procStart) > 0);
      SessionRecorder recorder(data.db, [] { return 0; });
      recorder.recover({process}, profiles, 12355);
      QCOMPARE(recorder.activeCount(), 1);
      QCOMPARE(recorder.activeSessions().first().startedAt, qint64(12345));
      return;
    }
    QFAIL("test process absent from procfs");
  }
  void fullHistoryPagesReachOlderSessions() {
    Database data;
    for (int i = 1; i <= 45; ++i)
      data.add(i * 100, 10);
    PlaySessionStore store(data.path);
    QSet<QString> keys;
    for (int offset = 0; offset < 45; offset += 8)
      for (const auto& row : store.historyForPaths({"/games/test.nsp"}, 8, offset))
        keys.insert(row.toMap().value("sessionKey").toString());
    QCOMPARE(keys.size(), 45);
  }
  void periodSurvivesOtherSettingsAndRestart() {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.toml");
    {
      AppSettings settings(path);
      settings.setStatsPeriod("all");
      settings.setReducedMotion(true);
    }
    AppSettings reopened(path);
    QCOMPARE(reopened.statsPeriod(), QString("all"));
    QVERIFY(reopened.applyBackupSettings({}, true));
    QCOMPARE(reopened.statsPeriod(), QString("all"));
  }
  void linkedInstallationsShareBacklogIdentity() {
    Database data;
    LinkedSource source;
    UnifiedGameModel games(data.path);
    games.addSourceModel(&source);
    QCOMPARE(games.rowCount(), 2);
    QVERIFY(games.linkGames(0, "RetroArch", "", "1"));
    QCOMPARE(games.rowCount(), 1);
    data.add(timestamp(2026, 1, 1, 12), 600, "/games/copy0.sfc");
    data.add(timestamp(2026, 3, 1, 12), 600, "/games/copy1.sfc");
    PlayStats stats(&games, data.path);
    stats.setPeriod("all");
    stats.refresh();
    QCOMPARE(stats.headline().value("gamesPlayed").toInt(), 1);
    QCOMPARE(stats.backlog().value("firstTimeGames").toInt(), 1);
    QCOMPARE(stats.backlog().value("oneAndDone").toInt(), 0);
    QCOMPARE(stats.backlog().value("returns").toInt(), 1);
    data.add(timestamp(2026, 4, 1, 12), 1000, "/games/unknown.bin");
    data.sql("UPDATE play_sessions SET source='RetroArch'");
    stats.refresh();
    bool unknown = false;
    for (const auto& entry : stats.bySystem()) {
      const auto row = entry.toMap();
      if (row.value("name").toString() == "RetroArch") {
        unknown = true;
        QCOMPARE(row.value("seconds").toLongLong(), qint64(1000));
        QVERIFY(!row.value("console").toBool());
      }
    }
    QVERIFY(unknown);
  }

  void documentsAndIdleScopesAreNotGames() {
    GameStop::GameIdentity game;
    game.installPath = "/games/test";
    for (const QString& program : {"kate", "nautilus", "rsync", "tail", "python3"}) {
      ProcessSnapshot p;
      p.pid = 4242;
      p.procStart = 100;
      p.comm = program;
      p.exePath = "/usr/bin/" + program;
      p.arguments = {program, "/games/test/config.ini"};
      QVERIFY(GameStop::plan(game, {p}, {}).isEmpty());
    }
    game.flatpak = true;
    game.flatpakAppId = "com.valvesoftware.Steam";
    game.winePrefixes = {"/games/test/pfx"};
    QVERIFY(GameStop::plan(game, {}, {}).isEmpty());
  }
  void presenceReconnectsForUnchangedActivity() {
    QTemporaryDir dir;
    const auto path = dir.filePath("discord.sock");
    std::atomic<int> accepted{0};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    std::unique_ptr<QThread> peer(QThread::create([&] {
      QLocalServer server;
      if (!server.listen(path)) {
        failed = true;
        ready = true;
        return;
      }
      ready = true;
      for (int connection = 0; connection < 2; ++connection) {
        if (!server.waitForNewConnection(4000)) {
          failed = true;
          return;
        }
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        const auto readFrame = [&]() {
          while (socket->bytesAvailable() < 8)
            if (!socket->waitForReadyRead(1500))
              return QByteArray{};
          auto header = socket->read(8);
          const auto size = qFromLittleEndian<quint32>(header.constData() + 4);
          while (socket->bytesAvailable() < size)
            if (!socket->waitForReadyRead(1500))
              return QByteArray{};
          return socket->read(size);
        };
        if (readFrame().isEmpty()) {
          failed = true;
          return;
        }
        socket->write(DiscordPresence::frame(1, R"({"evt":"READY"})"));
        socket->waitForBytesWritten(1000);
        auto command = QJsonDocument::fromJson(readFrame()).object();
        if (command.value("nonce").toString().isEmpty()) {
          failed = true;
          return;
        }
        QJsonObject reply{{"nonce", command.value("nonce")}, {"evt", QJsonValue::Null}};
        socket->write(
            DiscordPresence::frame(1, QJsonDocument(reply).toJson(QJsonDocument::Compact)));
        socket->waitForBytesWritten(1000);
        ++accepted;
        socket->disconnectFromServer();
      }
    }));
    peer->start();
    // Join on every assertion exit, including a failed server setup.
    const auto join = qScopeGuard([&] { peer->wait(); });
    QTRY_VERIFY(ready.load());
    QVERIFY(!failed.load());
    DiscordPresence::Client client("123", {path});
    const QJsonObject activity{{"details", "Test Game"}};
    QVERIFY(client.publishActivity(activity));
    QTRY_COMPARE(accepted.load(), 1);
    QTest::qWait(50);
    QVERIFY(client.publishActivity(activity));
    QTRY_COMPARE(accepted.load(), 2);
    QVERIFY(!failed.load());
  }

  // Scope A: a session refused by the database is written to the durable journal and
  // replayed after a restart, with its original boundaries and exactly once.
  void journalReplaysRefusedSessionAfterRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-replay");
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        QSqlQuery trigger(database);
        QVERIFY(trigger.exec(
            QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                           "SELECT RAISE(ABORT,'denied'); END")));
      }
      qint64 nowMs = 0;
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      const SessionMatch match{.pid = 42,
                               .procStart = 420,
                               .emulator = QStringLiteral("Ryujinx"),
                               .gamePath = QStringLiteral("/games/journaled.nsp")};
      recorder.sync({match}, 1000);
      nowMs = 60000;
      recorder.sync({}, 1060);
      QCOMPARE(recorder.pendingCloseCount(), 1);
      QVERIFY(QFileInfo::exists(journalPath));
      // Storage recovers after the game has gone and before any replay: the trigger is a
      // test stand-in for a database that was full or read-only and is writable again.
      {
        QSqlQuery dropTrigger(database);
        QVERIFY(dropTrigger.exec(QStringLiteral("DROP TRIGGER deny_insert")));
      }
    }
    QSqlDatabase::removeDatabase(connection);
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
        recorder.recover({}, ProcessProfileSet{}, 2000);
      }
      QSqlQuery query(database);
      QVERIFY(query.exec(
          QStringLiteral("SELECT game_path, started_at, ended_at, seconds FROM play_sessions")));
      QVERIFY2(query.next(), "the journaled session was not replayed");
      QCOMPARE(query.value(0).toString(), QStringLiteral("/games/journaled.nsp"));
      QCOMPARE(query.value(1).toLongLong(), qint64(1000));
      QCOMPARE(query.value(2).toLongLong(), qint64(1060));
      QCOMPARE(query.value(3).toLongLong(), qint64(60));
      QVERIFY2(!query.next(), "replay wrote the session more than once");
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A crash between the database commit and the journal acknowledgment must not double a
  // session: the stable key makes the replay a no-op.
  void journalReplayAfterCommitBeforeAckIsIdempotent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-ack");
    const QString key = QStringLiteral("11111111-2222-3333-4444-555555555555");
    QString incarnation;
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      incarnation = SessionDatabase::journalIncarnation(database);
      QVERIFY(!incarnation.isEmpty());
      QVERIFY(SessionDatabase::insertClosedSession(
          database, QStringLiteral("/games/idempotent.nsp"), QStringLiteral("Ryujinx"), 1000, 1060,
          60, 1, 1, key));
    }
    QSqlDatabase::removeDatabase(connection);
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = key;
      operation.gamePath = QStringLiteral("/games/idempotent.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      operation.pid = 1;
      operation.procStart = 1;
      operation.incarnation = incarnation;
      QVERIFY(journal.append(operation));
    }
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 1);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A game's whole history cleared before a pending record is replayed must stop that record
  // coming back, even when the game had no stored row at all.
  void journalDoesNotResurrectClearedHistory() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-clear");
    const QString path = QStringLiteral("/games/deleted.nsp");
    QString incarnation;
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      incarnation = SessionDatabase::journalIncarnation(database);
      QCOMPARE(SessionDatabase::deleteSessionsForPaths(database, {path}), 0);
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 0);
    }
    QSqlDatabase::removeDatabase(connection);
    // Now the same thing for a record written to the journal before the clear.
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("22222222-3333-4444-5555-666666666666");
      operation.gamePath = path;
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      operation.incarnation = incarnation;
      QVERIFY(journal.append(operation));
    }
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      QCOMPARE(SessionDatabase::deleteSessionsForPaths(database, {path}), 0);
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 0);
      QVector<SessionJournal::Operation> leftover;
      {
        SessionJournal journal(journalPath);
        QVERIFY(journal.open());
        leftover = journal.pending(10);
      }
      QVERIFY2(leftover.isEmpty(), "the stale record was not compacted away");
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A clear that happens while the recorder is still running must invalidate the in-memory
  // retry too, not only a journal replay on the next start.
  void sameRecorderRetryDoesNotResurrectClearedHistory() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-live-clear");
    const QString path = QStringLiteral("/games/live-clear.nsp");
    QSqlDatabase database;
    QVERIFY(SessionDatabase::open(database, dbPath, connection));
    {
      QSqlQuery trigger(database);
      QVERIFY(trigger.exec(
          QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                         "SELECT RAISE(ABORT,'denied'); END")));
    }
    qint64 nowMs = 0;
    {
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      const SessionMatch match{
          .pid = 77, .procStart = 770, .emulator = QStringLiteral("Ryujinx"), .gamePath = path};
      recorder.sync({match}, 1000);
      nowMs = 60000;
      recorder.sync({}, 1060);
      QCOMPARE(recorder.pendingCloseCount(), 1);
      // Storage recovers and the user then clears the game's history before the retry.
      {
        QSqlQuery dropTrigger(database);
        QVERIFY(dropTrigger.exec(QStringLiteral("DROP TRIGGER deny_insert")));
      }
      QCOMPARE(SessionDatabase::deleteSessionsForPaths(database, {path}), 0);
      // The still-running recorder retries and must drop the stale operation.
      nowMs += 100000;
      recorder.sync({}, 1200);
      QCOMPARE(recorder.pendingCloseCount(), 0);
    }
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
  }

  // Deleting one session must not invalidate a different pending session for the same game.
  void singleDeletionKeepsOtherPendingSessions() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-single-delete");
    const QString path = QStringLiteral("/games/shared.nsp");
    const QString oldKey = QStringLiteral("aaaa1111-0000-0000-0000-000000000001");
    const QString newKey = QStringLiteral("bbbb2222-0000-0000-0000-000000000002");
    QSqlDatabase database;
    QVERIFY(SessionDatabase::open(database, dbPath, connection));
    // An older finished session that the user will delete.
    const qint64 oldId =
        SessionDatabase::beginSession(database, path, QStringLiteral("Ryujinx"), 100, 5, 5, oldKey);
    QVERIFY(oldId > 0);
    QVERIFY(SessionDatabase::endSession(database, oldId, 200, 100));
    // A later session that storage refused, sitting in the journal, never asked to be deleted.
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = newKey;
      operation.gamePath = path;
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 300;
      operation.endedAt = 400;
      operation.seconds = 100;
      operation.incarnation = SessionDatabase::journalIncarnation(database);
      QVERIFY(journal.append(operation));
    }
    // Deleting only the old session tombstones its own key and leaves the clear epoch alone.
    QVERIFY(SessionDatabase::deleteSession(database, oldKey));
    QCOMPARE(SessionDatabase::gameClearEpoch(database, path), qint64(0));
    {
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
    }
    QSqlQuery query(database);
    QVERIFY(query.exec(
        QStringLiteral("SELECT session_key, seconds FROM play_sessions ORDER BY started_at")));
    QVERIFY2(query.next(), "the pending session was wrongly invalidated by a single deletion");
    QCOMPARE(query.value(0).toString(), newKey);
    QCOMPARE(query.value(1).toLongLong(), qint64(100));
    QVERIFY2(!query.next(), "the deleted session came back");
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
  }

  // A crash that tears the final append must keep the verified prefix, so already accepted
  // records still replay.
  void tornAppendKeepsVerifiedPrefix() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString journalPath = dir.filePath("torn.journal");
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("valid-record");
      operation.gamePath = QStringLiteral("/games/valid.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      QVERIFY(journal.append(operation));
    }
    const qint64 goodSize = QFileInfo(journalPath).size();
    // Simulate a crash mid-append: a length field with no payload.
    {
      QFile file(journalPath);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
      file.write(QByteArray::fromHex("00000010"));
      file.close();
    }
    QVERIFY(QFileInfo(journalPath).size() > goodSize);
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    QVERIFY(journal.recoveredTornTail());
    QVERIFY(!journal.recoveredCorrupt());
    const QVector<SessionJournal::Operation> pending = journal.pending(10);
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().key, QStringLiteral("valid-record"));
    // The torn tail is gone, so the file is back to its verified length.
    QCOMPARE(QFileInfo(journalPath).size(), goodSize);
    // An interior CRC failure is still treated as corruption, not a torn tail.
    {
      QFile file(journalPath);
      QVERIFY(file.open(QIODevice::ReadWrite));
      file.seek(QFileInfo(journalPath).size() - 1);
      file.write("Z");
      file.close();
    }
    SessionJournal damaged(journalPath);
    QVERIFY(damaged.open());
    QVERIFY(damaged.recoveredCorrupt());
    QVERIFY(QFileInfo::exists(journalPath + QStringLiteral(".corrupt")));
  }

  // Durable records past the in-memory cap must reach the database once storage recovers,
  // without a recorder restart.
  void journalDrainsBeyondMemoryCapWithoutRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-drain");
    QSqlDatabase database;
    QVERIFY(SessionDatabase::open(database, dbPath, connection));
    {
      QSqlQuery trigger(database);
      QVERIFY(trigger.exec(
          QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                         "SELECT RAISE(ABORT,'denied'); END")));
    }
    qint64 nowMs = 0;
    {
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      const int sessions = 70;
      for (int index = 0; index < sessions; ++index) {
        SessionMatch match;
        match.pid = 8000 + index;
        match.procStart = 9000 + index;
        match.emulator = QStringLiteral("Ryujinx");
        match.gamePath = QStringLiteral("/games/drain-%1.nsp").arg(index);
        nowMs = index * 10000;
        recorder.sync({match}, 1000 + index);
        nowMs += 5000;
        recorder.sync({}, 2000 + index);
      }
      QVERIFY(recorder.pendingCloseCount() <= 64);
      {
        QSqlQuery dropTrigger(database);
        QVERIFY(dropTrigger.exec(QStringLiteral("DROP TRIGGER deny_insert")));
      }
      // Advance a few retry intervals with no restart and let the journal drain.
      for (int pass = 0; pass < 5; ++pass) {
        nowMs += 100000;
        recorder.sync({}, 5000 + pass);
      }
    }
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 70);
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
  }

  // A replay into a database with a different incarnation is stale, which is how a restore or
  // replacement stops old records entering restored history.
  void replayRejectsRecordsFromAnotherIncarnation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-incarnation");
    QString incarnation;
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      incarnation = SessionDatabase::journalIncarnation(database);
      QVERIFY(!incarnation.isEmpty());
    }
    QSqlDatabase::removeDatabase(connection);
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("incarnation-record");
      operation.gamePath = QStringLiteral("/games/replaced.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      operation.incarnation = incarnation;
      QVERIFY(journal.append(operation));
    }
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      // A restore or replacement rewrites the identity.
      const QString replacement = SessionDatabase::resetJournalIncarnation(database);
      QVERIFY(!replacement.isEmpty());
      QVERIFY(replacement != incarnation);
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 0);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A restart reconciles a session whose process is gone and replays a refused close.
  void restartReconcilesOpenSessionAndReplaysClosed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-restart");
    const QString openKey = QStringLiteral("33333333-4444-5555-6666-777777777777");
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      QVERIFY(SessionDatabase::beginSession(database, QStringLiteral("/games/live.nsp"),
                                            QStringLiteral("Ryujinx"), 500, 999999, 999999,
                                            openKey) > 0);
      QSqlQuery update(database);
      update.prepare(QStringLiteral(
          "UPDATE play_sessions SET seconds = 200, heartbeat_at = 700 WHERE session_key = ?"));
      update.addBindValue(openKey);
      QVERIFY(update.exec());
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("44444444-5555-6666-7777-888888888888");
      operation.gamePath = QStringLiteral("/games/done.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      operation.incarnation = SessionDatabase::journalIncarnation(database);
      QVERIFY(journal.append(operation));
    }
    QSqlDatabase::removeDatabase(connection);
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
        recorder.recover({}, ProcessProfileSet{}, 2000);
      }
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions WHERE ended_at = 0")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 0);
      QVERIFY(query.exec(QStringLiteral(
          "SELECT ended_at, seconds FROM play_sessions WHERE game_path = '/games/live.nsp'")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toLongLong(), qint64(700));
      QCOMPARE(query.value(1).toLongLong(), qint64(200));
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 2);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // An active session whose insert was refused is checkpointed, so a killed recorder loses at
  // most the checkpoint interval instead of the whole session.
  void activeRefusedSessionIsCheckpointed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-active");
    const QString path = QStringLiteral("/games/active.nsp");
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        QSqlQuery trigger(database);
        QVERIFY(trigger.exec(
            QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                           "SELECT RAISE(ABORT,'denied'); END")));
      }
      qint64 nowMs = 0;
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      const SessionMatch match{
          .pid = 55, .procStart = 550, .emulator = QStringLiteral("Ryujinx"), .gamePath = path};
      recorder.sync({match}, 1000);
      // Advance with the game still running so a flush checkpoints the active session.
      nowMs = 30000;
      recorder.sync({match}, 1030);
      // Storage recovers: drop the trigger so the checkpoint can be replayed on restart.
      {
        QSqlQuery dropTrigger(database);
        QVERIFY(dropTrigger.exec(QStringLiteral("DROP TRIGGER deny_insert")));
      }
    }
    QSqlDatabase::removeDatabase(connection);
    // A fresh recorder replays the checkpoint into a live row that recovery then closes at the
    // checkpointed seconds, not at zero.
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 4000);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral(
          "SELECT ended_at, seconds FROM play_sessions WHERE game_path = '/games/active.nsp'")));
      QVERIFY2(query.next(), "the active checkpoint was lost");
      QVERIFY2(query.value(1).toLongLong() >= 30,
               "fewer seconds were recovered than the checkpoint observed");
      QCOMPARE(query.value(0).toLongLong() != 0, true);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // The journal refuses records at its cap and never evicts one it accepted.
  void journalCapacityRefusesWithoutEviction() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString journalPath = dir.filePath("capacity.journal");
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    int accepted = 0;
    for (int index = 0; index < 600; ++index) {
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("key-%1").arg(index);
      operation.gamePath = QStringLiteral("/games/g%1.nsp").arg(index);
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      if (journal.append(operation)) {
        ++accepted;
      }
    }
    QVERIFY2(journal.full(), "the journal did not report that it reached its cap");
    QCOMPARE(accepted, 512);
    const QVector<SessionJournal::Operation> pending = journal.pending(1000);
    QCOMPARE(pending.size(), 512);
    QCOMPARE(pending.first().key, QStringLiteral("key-0"));
  }

  // A malformed journal is set aside once and replaced, so corruption cannot poison every
  // startup.
  void journalMalformedFileIsSetAsideAndReplaced() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString journalPath = dir.filePath("bad.journal");
    {
      QFile file(journalPath);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("this is not a journal");
      file.close();
    }
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    QVERIFY(journal.recoveredCorrupt());
    QVERIFY(QFileInfo::exists(journalPath + QStringLiteral(".corrupt")));
    SessionJournal::Operation operation;
    operation.key = QStringLiteral("fresh");
    operation.gamePath = QStringLiteral("/games/fresh.nsp");
    operation.source = QStringLiteral("Ryujinx");
    operation.startedAt = 1000;
    operation.endedAt = 1060;
    operation.seconds = 60;
    QVERIFY(journal.append(operation));
    QCOMPARE(journal.pending(10).size(), 1);
  }

  // Storage that cannot hold the journal is reported, not silently accepted.
  void journalUnavailableStorageIsReported() {
    SessionJournal journal(QStringLiteral("/proc/omakade-nonexistent/journal"));
    QVERIFY(!journal.open());
    QVERIFY(!journal.available());
    SessionJournal::Operation operation;
    operation.key = QStringLiteral("x");
    operation.gamePath = QStringLiteral("/games/x.nsp");
    QVERIFY(!journal.append(operation));
  }

  // The stable key resolves the right row even after another insert moved lastInsertId, and a
  // repeated close is a no-op rather than a duplicate.
  void stableSessionKeyResolvesCorrectRow() {
    const QString connection = QStringLiteral("stable-key");
    QSqlDatabase database;
    QVERIFY(SessionDatabase::open(database, QStringLiteral(":memory:"), connection));
    const QString keyA = QStringLiteral("55555555-6666-7777-8888-999999999999");
    const qint64 idA = SessionDatabase::beginSession(database, QStringLiteral("/games/a.nsp"),
                                                     QStringLiteral("Ryujinx"), 1000, 1, 1, keyA);
    QVERIFY(idA > 0);
    // A different session inserts afterwards, moving the connection's last insert id.
    const qint64 idB = SessionDatabase::beginSession(database, QStringLiteral("/games/b.nsp"),
                                                     QStringLiteral("Ryujinx"), 1000, 1, 1, {});
    QVERIFY(idB > 0);
    QVERIFY(idA != idB);
    const qint64 retried = SessionDatabase::beginSession(
        database, QStringLiteral("/games/a.nsp"), QStringLiteral("Ryujinx"), 1000, 1, 1, keyA);
    QCOMPARE(retried, idA);
    QVERIFY(SessionDatabase::insertClosedSession(database, QStringLiteral("/games/a.nsp"),
                                                 QStringLiteral("Ryujinx"), 1000, 1060, 60, 1, 1,
                                                 keyA));
    QVERIFY(SessionDatabase::insertClosedSession(database, QStringLiteral("/games/a.nsp"),
                                                 QStringLiteral("Ryujinx"), 1000, 1060, 60, 1, 1,
                                                 keyA));
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT COUNT(*), SUM(seconds) FROM play_sessions WHERE game_path = '/games/a.nsp'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QCOMPARE(query.value(1).toLongLong(), qint64(60));
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
  }
  // A temporary read failure (a locked database) must let a record be retried, not discard it
  // as if the database had been replaced.
  void transientIdentityReadFailureDoesNotDiscardRecord() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-read-failure");
    QString incarnation;
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      incarnation = SessionDatabase::journalIncarnation(database);
    }
    QSqlDatabase::removeDatabase(connection);
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("retry-after-read-failure");
      operation.gamePath = QStringLiteral("/games/retry.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      operation.incarnation = incarnation;
      QVERIFY(journal.append(operation));
    }
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      // Make the identity read fail, as a locked database would.
      QSqlQuery drop(database);
      QVERIFY(drop.exec(QStringLiteral("DROP TABLE session_journal_state")));
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      // The record must survive the failed read for a later retry.
      {
        SessionJournal journal(journalPath);
        QVERIFY(journal.open());
        QVERIFY2(journal.pending(10).size() == 1,
                 "a transient identity read failure discarded a durable record");
      }
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // An outage before the identity cache exists leaves the operation unresolved. Storage
  // recovery without a restore must later bind and write exactly that one accepted session.
  void firstIdentityOutageRecoversOneSession() {
    Database data;
    const QString journalPath = data.path + QStringLiteral(".journal");
    QVERIFY(!SessionDatabase::journalIncarnation(data.db).isEmpty());
    data.sql(QStringLiteral("PRAGMA journal_mode=DELETE"));
    data.sql(QStringLiteral("PRAGMA busy_timeout=1"));
    const QString lockConnection = data.name + QStringLiteral("-lock");
    QSqlDatabase lock = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lockConnection);
    lock.setDatabaseName(data.path);
    QVERIFY(lock.open());
    QSqlQuery exclusive(lock);
    QVERIFY(exclusive.exec(QStringLiteral("BEGIN EXCLUSIVE")));
    qint64 nowMs = 0;
    std::unique_ptr<SessionRecorder> recorder;
    recorder = std::make_unique<SessionRecorder>(data.db, [&nowMs] { return nowMs; }, journalPath);
    const SessionMatch match{.pid = 81,
                             .procStart = 810,
                             .emulator = QStringLiteral("Ryujinx"),
                             .gamePath = QStringLiteral("/games/first-outage.nsp")};
    recorder->sync({match}, 1000);
    nowMs = 60000;
    recorder->sync({}, 1060);
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      const auto pending = journal.pending(10);
      QCOMPARE(pending.size(), 1);
      QVERIFY(pending.first().incarnation.isEmpty());
    }
    QVERIFY(exclusive.exec(QStringLiteral("ROLLBACK")));
    nowMs += 31000;
    recorder->sync({}, 1091);
    lock.close();
    lock = {};
    QSqlDatabase::removeDatabase(lockConnection);
    QSqlQuery query(data.db);
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      QCOMPARE(journal.pending(10).size(), 0);
    }
  }

  // Replaying an older checkpoint must not lower progress or move the heartbeat to now.
  void replayOfOlderCheckpointDoesNotLowerProgress() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-older-checkpoint");
    const QString key = QStringLiteral("77777777-8888-9999-aaaa-bbbbbbbbbbbb");
    const qint64 start = 1000;
    const qint64 observedNewer = 5000;
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      const qint64 id =
          SessionDatabase::beginSession(database, QStringLiteral("/games/progress.nsp"),
                                        QStringLiteral("Ryujinx"), start, 1, 1, key);
      QVERIFY(id > 0);
      QVERIFY(SessionDatabase::updateProgress(database, id, 120, observedNewer));
      // An older checkpoint with less time and an earlier observation.
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = key;
      operation.gamePath = QStringLiteral("/games/progress.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = start;
      operation.seconds = 60;
      operation.observedAt = observedNewer - 1000;
      operation.open = true;
      operation.incarnation = SessionDatabase::journalIncarnation(database);
      QVERIFY(journal.append(operation));
    }
    QSqlDatabase::removeDatabase(connection);
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 999999);
      QSqlQuery query(database);
      query.prepare(
          QStringLiteral("SELECT seconds, heartbeat_at FROM play_sessions WHERE session_key = ?"));
      query.addBindValue(key);
      QVERIFY(query.exec());
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toLongLong(), qint64(120));
      QCOMPARE(query.value(1).toLongLong(), observedNewer);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A failed active-session replay must be retried as an open session, not written as a closed
  // session with its end clamped to its start.
  void failedCheckpointReplayStaysOpen() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-open-retry");
    const QString path = QStringLiteral("/games/open-retry.nsp");
    qint64 persisted = 0;
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        QSqlQuery trigger(database);
        QVERIFY(trigger.exec(
            QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                           "SELECT RAISE(ABORT,'denied'); END")));
      }
      qint64 nowMs = 0;
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      const SessionMatch match{
          .pid = 61, .procStart = 610, .emulator = QStringLiteral("Ryujinx"), .gamePath = path};
      recorder.sync({match}, 1000);
      nowMs = 30000;
      recorder.sync({match}, 1030);
      persisted = 30;
    }
    QSqlDatabase::removeDatabase(connection);
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      qint64 nowMs = 0;
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      // The trigger is still present, so the checkpoint replay fails and is queued.
      recorder.recover({}, ProcessProfileSet{}, 2000);
      {
        QSqlQuery dropTrigger(database);
        QVERIFY(dropTrigger.exec(QStringLiteral("DROP TRIGGER deny_insert")));
      }
      nowMs = 100000;
      recorder.sync({}, 3000);
      QSqlQuery query(database);
      query.prepare(QStringLiteral(
          "SELECT started_at, ended_at, seconds FROM play_sessions WHERE game_path = ?"));
      query.addBindValue(path);
      QVERIFY(query.exec());
      QVERIFY2(query.next(), "the failed checkpoint replay was lost");
      // A dead session is closed at its observation time with a real span, never clamped to a
      // zero-length session with time on it.
      QVERIFY2(query.value(1).toLongLong() != 0, "the replayed checkpoint was left open");
      QVERIFY2(query.value(1).toLongLong() > query.value(0).toLongLong(),
               "the replayed checkpoint had no span");
      QVERIFY2(query.value(2).toLongLong() >= persisted,
               "the replayed checkpoint recorded less time than was observed");
      QVERIFY2(!query.next(), "the checkpoint produced more than one row");
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A partial header left by a torn creation must not swallow the next accepted record.
  void partialHeaderThenAppendSurvivesReopen() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString journalPath = dir.filePath("partial.journal");
    {
      QFile file(journalPath);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("OMKJR", 5);
      file.close();
    }
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("after-partial-header");
      operation.gamePath = QStringLiteral("/games/partial.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      QVERIFY(journal.append(operation));
    }
    SessionJournal reopened(journalPath);
    QVERIFY(reopened.open());
    QVERIFY(!reopened.recoveredCorrupt());
    const QVector<SessionJournal::Operation> pending = reopened.pending(10);
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().key, QStringLiteral("after-partial-header"));
  }

  // A journal full by byte size, before the entry cap, must still report full so the persistent
  // Settings warning appears.
  void journalStatusReportsFullBySize() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString journalPath = dir.filePath("size-full.journal");
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    const QString longPath = QStringLiteral("/games/%1.nsp").arg(QString(4000, QLatin1Char('x')));
    int accepted = 0;
    for (int index = 0; index < 600; ++index) {
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("size-key-%1").arg(index);
      operation.gamePath = longPath;
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      if (journal.append(operation)) {
        ++accepted;
      }
    }
    QVERIFY(journal.full());
    QVERIFY2(accepted < 512, "the byte cap should be reached before the entry cap");
    const SessionJournal::Status status = SessionJournal::status(journalPath);
    QVERIFY2(status.full, "a journal full by size did not report full");
    QVERIFY(!status.corrupt);
    QCOMPARE(status.pending, accepted);
  }
  // A record accepted while the database was unwritable carries no identity. A replace restore
  // must not let it resurrect the history the restore removed.
  void anonymousRecordDoesNotSurviveReplaceRestore() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-anonymous-restore");
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      QVERIFY(!SessionDatabase::journalIncarnation(database).isEmpty());
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("anonymous-before-restore");
      operation.gamePath = QStringLiteral("/games/anonymous.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      // No incarnation: what a record accepted during a lock looks like.
      QVERIFY(journal.append(operation));
    }
    QSqlDatabase::removeDatabase(connection);
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      // A replace restore rotates the identity (and removes the journal).
      QVERIFY(!SessionDatabase::resetJournalIncarnation(database).isEmpty());
      // Recreate the anonymous record as if the file survived a manual replacement.
      {
        SessionJournal journal(journalPath);
        QVERIFY(journal.open());
        SessionJournal::Operation operation;
        operation.key = QStringLiteral("anonymous-before-restore");
        operation.gamePath = QStringLiteral("/games/anonymous.nsp");
        operation.source = QStringLiteral("Ryujinx");
        operation.startedAt = 1000;
        operation.endedAt = 1060;
        operation.seconds = 60;
        QVERIFY(journal.append(operation));
      }
      SessionRecorder recorder(database, [] { return qint64(0); }, journalPath);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 0);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // A journal-only checkpoint replayed after a live process appears must adopt that process
  // under the original session key instead of letting the next poll create a second row.
  void delayedReplayAdoptsSurvivingSameGameProcess() {
    Database data;
    const QString journalPath = data.path + QStringLiteral(".journal");
    ProcessSnapshot self;
    for (const ProcessSnapshot& process : ProcFs::listProcesses()) {
      if (process.pid == QCoreApplication::applicationPid()) {
        self = process;
        break;
      }
    }
    QVERIFY(self.pid > 0 && self.procStart > 0);
    const QString gamePath = QStringLiteral("/games/delayed-live.nsp");
    SessionJournal::Operation operation;
    operation.key = QStringLiteral("delayed-live");
    operation.gamePath = gamePath;
    operation.source = QStringLiteral("Ryujinx");
    operation.startedAt = 1000;
    operation.seconds = 60;
    operation.observedAt = 1060;
    operation.pid = self.pid;
    operation.procStart = self.procStart;
    operation.open = true;
    operation.incarnation = SessionDatabase::journalIncarnation(data.db);
    {
      SessionJournal journal(journalPath);
      QVERIFY(journal.open());
      QVERIFY(journal.append(operation));
    }
    data.sql(QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                            "SELECT RAISE(ABORT,'denied'); END"));
    {
      qint64 nowMs = 100000;
      SessionRecorder recorder(data.db, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      recorder.recover({}, ProcessProfileSet{}, 2000);
      QCOMPARE(recorder.pendingCloseCount(), 1);
      data.sql(QStringLiteral("DROP TRIGGER deny_insert"));
      self.arguments = {QCoreApplication::applicationFilePath(), gamePath};
      self.comm = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
      ProcessProfileSet profiles;
      profiles.romExtensions.insert(QStringLiteral("nsp"));
      profiles.emulators.append(
          {.name = QStringLiteral("Ryujinx"), .binaries = {self.comm}, .rescanSource = {}});
      ++nowMs;
      recorder.sync(ProcessMatcher::match({self}, profiles), 2031);
      QCOMPARE(recorder.activeCount(), 1);
      recorder.sync({}, 2062);
      QCOMPARE(recorder.activeCount(), 0);
    }
    QSqlQuery query(data.db);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*), SUM(ended_at = 0), COUNT(DISTINCT session_key) FROM play_sessions"));
    QVERIFY(query.exec() && query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QCOMPARE(query.value(1).toInt(), 0);
    QCOMPARE(query.value(2).toInt(), 1);
  }

  // The current match proves a live process changed games. Its old history closes at the
  // observed boundary while the new game becomes the only open row.
  void delayedReplayClosesSurvivingChangedGame() {
    Database data;
    const QString journalPath = data.path + QStringLiteral(".journal");
    ProcessSnapshot self;
    for (const ProcessSnapshot& process : ProcFs::listProcesses()) {
      if (process.pid == QCoreApplication::applicationPid()) {
        self = process;
        break;
      }
    }
    QVERIFY(self.pid > 0 && self.procStart > 0);
    const QString oldPath = QStringLiteral("/games/old-changed.nsp");
    const QString newPath = QStringLiteral("/games/new-changed.nsp");
    SessionJournal::Operation operation;
    operation.key = QStringLiteral("changed-game");
    operation.gamePath = oldPath;
    operation.source = QStringLiteral("Ryujinx");
    operation.startedAt = 1000;
    operation.seconds = 60;
    operation.observedAt = 1060;
    operation.pid = self.pid;
    operation.procStart = self.procStart;
    operation.open = true;
    operation.incarnation = SessionDatabase::journalIncarnation(data.db);
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    QVERIFY(journal.append(operation));
    data.sql(QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                            "SELECT RAISE(ABORT,'denied'); END"));
    qint64 nowMs = 100000;
    SessionRecorder recorder(data.db, [&nowMs] { return nowMs; }, journalPath);
    recorder.setFlushIntervalMs(1);
    recorder.recover({}, ProcessProfileSet{}, 2000);
    QCOMPARE(recorder.pendingCloseCount(), 1);
    data.sql(QStringLiteral("DROP TRIGGER deny_insert"));
    self.arguments = {QCoreApplication::applicationFilePath(), newPath};
    self.comm = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    ProcessProfileSet profiles;
    profiles.romExtensions.insert(QStringLiteral("nsp"));
    profiles.emulators.append(
        {.name = QStringLiteral("Ryujinx"), .binaries = {self.comm}, .rescanSource = {}});
    ++nowMs;
    recorder.sync(ProcessMatcher::match({self}, profiles), 2031);
    QSqlQuery oldRow(data.db);
    oldRow.prepare(QStringLiteral("SELECT ended_at FROM play_sessions WHERE game_path = ?"));
    oldRow.addBindValue(oldPath);
    QVERIFY(oldRow.exec() && oldRow.next());
    QCOMPARE(oldRow.value(0).toLongLong(), qint64(1060));
    QSqlQuery newRow(data.db);
    newRow.prepare(QStringLiteral("SELECT ended_at FROM play_sessions WHERE game_path = ?"));
    newRow.addBindValue(newPath);
    QVERIFY(newRow.exec() && newRow.next());
    QCOMPARE(newRow.value(0).toLongLong(), qint64(0));
    QSqlQuery count(data.db);
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM play_sessions")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 2);
    QCOMPARE(recorder.activeCount(), 1);
  }

  // A final close is a second database step and can fail after its checkpoint replay. The
  // key stays pending until the close succeeds and the row becomes deletable.
  void failedFinalCloseRemainsRetryable() {
    Database data;
    const QString journalPath = data.path + QStringLiteral(".journal");
    const QString gamePath = QStringLiteral("/games/final-close.nsp");
    SessionJournal::Operation operation;
    operation.key = QStringLiteral("retry-final-close");
    operation.gamePath = gamePath;
    operation.source = QStringLiteral("Ryujinx");
    operation.startedAt = 1000;
    operation.seconds = 60;
    operation.observedAt = 1060;
    operation.pid = 99999991;
    operation.procStart = 10;
    operation.open = true;
    operation.incarnation = SessionDatabase::journalIncarnation(data.db);
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    QVERIFY(journal.append(operation));
    data.sql(QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                            "SELECT RAISE(ABORT,'denied'); END"));
    qint64 nowMs = 100000;
    SessionRecorder recorder(data.db, [&nowMs] { return nowMs; }, journalPath);
    recorder.setFlushIntervalMs(1);
    recorder.recover({}, ProcessProfileSet{}, 2000);
    QCOMPARE(recorder.pendingCloseCount(), 1);
    data.sql(QStringLiteral("DROP TRIGGER deny_insert"));
    data.sql(
        QStringLiteral("CREATE TRIGGER deny_close BEFORE UPDATE OF ended_at ON play_sessions BEGIN "
                       "SELECT RAISE(ABORT,'denied-close'); END"));
    ++nowMs;
    recorder.sync({}, 2031);
    QCOMPARE(recorder.pendingCloseCount(), 1);
    {
      SessionJournal retry(journalPath);
      QVERIFY(retry.open());
      QCOMPARE(retry.pending(10).size(), 1);
    }
    data.sql(QStringLiteral("DROP TRIGGER deny_close"));
    for (int pass = 0; pass < 5; ++pass) {
      ++nowMs;
      recorder.sync({}, 3000 + pass);
    }
    QSqlQuery query(data.db);
    QVERIFY(query.exec(QStringLiteral("SELECT ended_at FROM play_sessions")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toLongLong(), qint64(1060));
    {
      SessionJournal drained(journalPath);
      QVERIFY(drained.open());
      QCOMPARE(drained.pending(10).size(), 0);
    }
    QCOMPARE(SessionDatabase::deleteSessionsForPaths(data.db, {gamePath}), 1);
  }

  // A recovered active session whose process is gone must be closed, so a delayed replay does
  // not leave an open row that history deletion refuses to remove.
  void delayedRecoveryClosesDeadSession() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath("library.sqlite3");
    const QString journalPath = dbPath + QStringLiteral(".journal");
    const QString connection = QStringLiteral("journal-dead-open");
    const QString path = QStringLiteral("/games/dead-open.nsp");
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      {
        QSqlQuery trigger(database);
        QVERIFY(trigger.exec(
            QStringLiteral("CREATE TRIGGER deny_insert BEFORE INSERT ON play_sessions BEGIN "
                           "SELECT RAISE(ABORT,'denied'); END")));
      }
      qint64 nowMs = 0;
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      const SessionMatch match{
          .pid = 63, .procStart = 630, .emulator = QStringLiteral("Ryujinx"), .gamePath = path};
      recorder.sync({match}, 1000);
      nowMs = 30000;
      recorder.sync({match}, 1030);
    }
    QSqlDatabase::removeDatabase(connection);
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, dbPath, connection));
      qint64 nowMs = 0;
      SessionRecorder recorder(database, [&nowMs] { return nowMs; }, journalPath);
      recorder.setFlushIntervalMs(1);
      // The recorder restarts while storage is still refusing; the checkpoint replay fails and
      // is queued. Storage then recovers and several polls pass with no running game.
      recorder.recover({}, ProcessProfileSet{}, 2000);
      {
        QSqlQuery dropTrigger(database);
        QVERIFY(dropTrigger.exec(QStringLiteral("DROP TRIGGER deny_insert")));
      }
      for (int pass = 0; pass < 5; ++pass) {
        nowMs += 100000;
        recorder.sync({}, 3000 + pass);
      }
      QSqlQuery query(database);
      query.prepare(
          QStringLiteral("SELECT ended_at, seconds FROM play_sessions WHERE game_path = ?"));
      query.addBindValue(path);
      QVERIFY(query.exec());
      QVERIFY2(query.next(), "the recovered session was lost");
      QVERIFY2(query.value(0).toLongLong() != 0,
               "a recovered session with no running game stayed open");
      // A closed session must be removable through history deletion.
      QCOMPARE(SessionDatabase::deleteSessionsForPaths(database, {path}), 1);
    }
    QSqlDatabase::removeDatabase(connection);
  }

  // Repeated checkpoints for one session must not exhaust the journal, and a genuinely full
  // journal (many sessions) must report full for the persistent warning.
  void oneSessionCheckpointsDoNotExhaustJournal() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString journalPath = dir.filePath("checkpoints.journal");
    SessionJournal journal(journalPath);
    QVERIFY(journal.open());
    // Many checkpoints for a single session: one record, never full.
    for (int index = 0; index < 600; ++index) {
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("one-session");
      operation.gamePath = QStringLiteral("/games/one.nsp");
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.seconds = index;
      operation.observedAt = 1000 + index;
      operation.open = true;
      QVERIFY(journal.append(operation));
    }
    QVERIFY(!journal.full());
    QCOMPARE(journal.recordCount(), 1);
    SessionJournal::Status status = SessionJournal::status(journalPath);
    QVERIFY(!status.full);
    QCOMPARE(status.pending, 1);
    // Many distinct sessions do reach the entry cap, and the status says so.
    int accepted = 0;
    for (int index = 0; index < 600; ++index) {
      SessionJournal::Operation operation;
      operation.key = QStringLiteral("distinct-%1").arg(index);
      operation.gamePath = QStringLiteral("/games/d%1.nsp").arg(index);
      operation.source = QStringLiteral("Ryujinx");
      operation.startedAt = 1000;
      operation.endedAt = 1060;
      operation.seconds = 60;
      if (journal.append(operation)) {
        ++accepted;
      }
    }
    QVERIFY2(journal.full(), "many distinct sessions did not reach the entry cap");
    status = SessionJournal::status(journalPath);
    QVERIFY2(status.full, "a full journal did not report full for the warning");
  }
};
QTEST_GUILESS_MAIN(ReleaseHardeningTests)
#include "ReleaseHardeningTests.moc"
