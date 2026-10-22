#include "highlights/ClipLibrary.h"
#include "highlights/HighlightService.h"
#include "highlights/HighlightSettings.h"
#include "highlights/ReplayBuffer.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionTitleIndex.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kFakeRecorder = QStringLiteral(OMAKADE_FIXTURE_DIR "/fake-gpu-screen-recorder");

QJsonObject lastRequest(const QString& log) {
  QFile file(log);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QJsonObject last;
  for (const QByteArray& line : file.readAll().split('\n')) {
    const QJsonObject entry = QJsonDocument::fromJson(line).object();
    if (entry.contains(QStringLiteral("request"))) last = entry.value(QStringLiteral("request")).toObject();
  }
  return last;
}

int starts(const QString& log) {
  QFile file(log);
  if (!file.open(QIODevice::ReadOnly)) return 0;
  return int(file.readAll().count("{\"start\""));
}
} // namespace

class HighlightTests final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  void settingsParse();
  void folderNamesAreSafe();
  void filingMovesAndRecords();
  void filingKeepsVideoWhenDatabaseRefuses();
  void listingSkipsMissingFiles();
  void titleForPathNeedsOneAnswer();
  void argumentsDescribeReplayBuffer();
  void bufferSavesThroughIpc();
  void bufferReportsRecorderCrash();
  void bufferGivesUpWithoutSocket();
  void saveFailureIsReported();
  void stopAnswersPendingSaveOnce();
  void serviceFollowsGameAndMonitor();
  void serviceKeepsBufferForHiddenGame();
  void serviceStopsWhenTurnedOff();
  void serviceFilesClipUnderTitle();
  void serviceRefusesWithReason();
  void serviceRetriesThenReportsOnce();
  void serviceTrimsGuideTime();

private:
  HighlightService::Environment environment();
  void writeConfig(const QString& contents);

  QTemporaryDir m_root;
  QString m_config;
  QString m_log;
  QStringList m_notifications;
  QSet<qint64> m_alive;
  int m_databases = 0;
};

void HighlightTests::initTestCase() {
  QVERIFY(m_root.isValid());
  QVERIFY(QFileInfo(kFakeRecorder).isExecutable());
  qputenv("XDG_RUNTIME_DIR", (m_root.path() + QStringLiteral("/runtime")).toUtf8());
  qputenv("XDG_CACHE_HOME", (m_root.path() + QStringLiteral("/cache")).toUtf8());
  QVERIFY(QDir().mkpath(m_root.path() + QStringLiteral("/runtime")));
  QFile::setPermissions(m_root.path() + QStringLiteral("/runtime"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void HighlightTests::init() {
  static int round = 0;
  const QString base = m_root.path() + QStringLiteral("/case-%1").arg(++round);
  QVERIFY(QDir().mkpath(base));
  m_config = base + QStringLiteral("/config.toml");
  m_log = base + QStringLiteral("/recorder.log");
  m_notifications.clear();
  m_alive = {100};
  qputenv("FAKE_GSR_LOG", m_log.toUtf8());
  qunsetenv("FAKE_GSR_MODE");
  qunsetenv("FAKE_GSR_VIDEO");
  writeConfig(QStringLiteral("highlights = true\nhighlight_seconds = 30\n"));
}

void HighlightTests::cleanup() { qunsetenv("FAKE_GSR_MODE"); }

void HighlightTests::writeConfig(const QString& contents) {
  QFile file(m_config);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(contents.toUtf8());
}

HighlightService::Environment HighlightTests::environment() {
  HighlightService::Environment environment;
  environment.configPath = m_config;
  environment.root = QFileInfo(m_config).absolutePath() + QStringLiteral("/Videos/Omakade");
  environment.recorder = kFakeRecorder;
  environment.alive = [this](qint64 pid, qint64) { return m_alive.contains(pid); };
  environment.notify = [this](const QString& title, const QString& body, const QString&) {
    m_notifications.append(title + QStringLiteral(": ") + body);
  };
  environment.openDatabase = [this](QSqlDatabase& database) {
    return SessionDatabase::open(database, QFileInfo(m_config).absolutePath() + QStringLiteral("/library.sqlite3"),
                                 QStringLiteral("highlight-tests-%1").arg(++m_databases));
  };
  environment.now = [] { return qint64(1792000000); };
  return environment;
}

void HighlightTests::settingsParse() {
  const auto defaults = HighlightSettings::parse({});
  QCOMPARE(defaults.enabled, false);
  QCOMPARE(defaults.clipSeconds, 30);
  QCOMPARE(defaults.achievementClips, true);
  const auto set = HighlightSettings::parse(
      QStringLiteral("other = 1\nhighlights = true\nhighlight_seconds = 60\nhighlight_achievements = false\n"));
  QCOMPARE(set.enabled, true);
  QCOMPARE(set.clipSeconds, 60);
  QCOMPARE(set.achievementClips, false);
  // A hand-edited length snaps to one the settings offer.
  QCOMPARE(HighlightSettings::parse(QStringLiteral("highlight_seconds = 45\n")).clipSeconds, 30);
  QCOMPARE(HighlightSettings::parse(QStringLiteral("highlight_seconds = 999999\n")).clipSeconds, 120);
  QCOMPARE(HighlightSettings::parse(QStringLiteral("highlight_seconds = 0\n")).clipSeconds, 15);
  // Only whole lines count, so a similar key is not mistaken for this one.
  QCOMPARE(HighlightSettings::parse(QStringLiteral("highlights_old = true\n")).enabled, false);
  QVERIFY(HighlightSettings::bufferSeconds(set) > set.clipSeconds);
  QCOMPARE(HighlightSettings::load(QStringLiteral("/nonexistent/config.toml")).enabled, false);
}

void HighlightTests::folderNamesAreSafe() {
  QCOMPARE(ClipLibrary::folderName(QStringLiteral("Bloodborne")), QStringLiteral("Bloodborne"));
  QCOMPARE(ClipLibrary::folderName(QStringLiteral("Half-Life 2: Episode One")), QStringLiteral("Half-Life 2 Episode One"));
  QCOMPARE(ClipLibrary::folderName(QStringLiteral("../../etc")), QStringLiteral("Game"));
  QCOMPARE(ClipLibrary::folderName(QStringLiteral("a/b\\c")), QStringLiteral("a b c"));
  QCOMPARE(ClipLibrary::folderName(QStringLiteral("  ")), QStringLiteral("Game"));
  QCOMPARE(ClipLibrary::folderName(QStringLiteral("Title...")), QStringLiteral("Title"));
  QCOMPARE(ClipLibrary::folderName(QStringLiteral(".hidden")), QStringLiteral("Game"));
  QCOMPARE(ClipLibrary::folderName(QString(300, QLatin1Char('x'))).size(), 120);
}

void HighlightTests::filingMovesAndRecords() {
  QSqlDatabase database;
  const QString base = QFileInfo(m_config).absolutePath();
  QVERIFY(SessionDatabase::open(database, base + QStringLiteral("/library.sqlite3"), QStringLiteral("filing")));
  const QString recorded = base + QStringLiteral("/Replay.mp4");
  for (int round = 0; round < 2; ++round) {
    QFile file(recorded);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("video");
    file.close();
    ClipLibrary::Clip clip;
    clip.source = QStringLiteral("Steam");
    clip.gameKey = QStringLiteral("367520");
    clip.title = QStringLiteral("Hollow Knight");
    clip.createdAt = 1792000000;
    clip.seconds = 30;
    clip.trigger = QStringLiteral("achievement");
    clip.label = QStringLiteral("Falsehood");
    QString error;
    const auto filed = ClipLibrary::file(database, recorded, base + QStringLiteral("/Videos"), clip, &error);
    QVERIFY2(filed.id > 0, qPrintable(error));
    QVERIFY(!QFileInfo::exists(recorded));
    QVERIFY(QFileInfo(filed.path).isFile());
    QCOMPARE(QFileInfo(filed.path).dir().dirName(), QStringLiteral("Hollow Knight"));
    // Two clips in the same second get different names rather than one replacing the other.
    QVERIFY(QFileInfo(filed.path).fileName().startsWith(QStringLiteral("Hollow Knight ")));
    if (round == 1) QVERIFY(filed.path.endsWith(QStringLiteral(" (2).mp4")));
  }
  const auto clips = ClipLibrary::forGame(database, QStringLiteral("Steam"), QStringLiteral("367520"));
  QCOMPARE(clips.size(), 2);
  QCOMPARE(clips.first().label, QStringLiteral("Falsehood"));
  QCOMPARE(clips.first().trigger, QStringLiteral("achievement"));
  QCOMPARE(ClipLibrary::recent(database, 1).size(), 1);
  QVERIFY(ClipLibrary::forget(database, clips.first().id));
  QCOMPARE(ClipLibrary::forGame(database, QStringLiteral("Steam"), QStringLiteral("367520")).size(), 1);
  QVERIFY(ClipLibrary::forGame(database, QStringLiteral("RetroArch"), QStringLiteral("367520")).isEmpty());
  database.close();
}

void HighlightTests::filingKeepsVideoWhenDatabaseRefuses() {
  const QString base = QFileInfo(m_config).absolutePath();
  const QString recorded = base + QStringLiteral("/Replay.mp4");
  QFile file(recorded);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("video");
  file.close();
  QSqlDatabase closed;
  ClipLibrary::Clip clip;
  clip.title = QStringLiteral("Game");
  QString error;
  const auto filed = ClipLibrary::file(closed, recorded, base + QStringLiteral("/Videos"), clip, &error);
  QCOMPARE(filed.id, 0);
  QVERIFY(!error.isEmpty());
  // Losing the video would be worse than Omakade not listing it.
  QVERIFY(QFileInfo(filed.path).isFile());
  // A file that never arrived is refused without inventing a path.
  const auto missing = ClipLibrary::file(closed, base + QStringLiteral("/none.mp4"), base, clip, &error);
  QVERIFY(missing.path.isEmpty());
}

void HighlightTests::listingSkipsMissingFiles() {
  QSqlDatabase database;
  const QString base = QFileInfo(m_config).absolutePath();
  QVERIFY(SessionDatabase::open(database, base + QStringLiteral("/library.sqlite3"), QStringLiteral("listing")));
  QSqlQuery query(database);
  QVERIFY(query.exec(QStringLiteral("INSERT INTO highlight_clips (path, source, game_key, title, created_at) "
                                    "VALUES ('/nonexistent/clip.mp4', 'Steam', '1', 'Gone', 5)")));
  QVERIFY(ClipLibrary::forGame(database, QStringLiteral("Steam"), QStringLiteral("1")).isEmpty());
  QVERIFY(ClipLibrary::recent(database, 10).isEmpty());
  database.close();
}

void HighlightTests::titleForPathNeedsOneAnswer() {
  QSqlDatabase database;
  const QString base = QFileInfo(m_config).absolutePath();
  QVERIFY(SessionDatabase::open(database, base + QStringLiteral("/library.sqlite3"), QStringLiteral("titles")));
  QSqlQuery query(database);
  QVERIFY(query.exec(QStringLiteral("CREATE TABLE shadps4_games (name TEXT, path TEXT, game_id TEXT)")));
  QVERIFY(query.exec(QStringLiteral("INSERT INTO shadps4_games VALUES ('Bloodborne', '/games/CUSA03173/eboot.bin', 'CUSA03173')")));
  QVERIFY(query.exec(QStringLiteral("CREATE TABLE ryujinx_games (name TEXT, path TEXT, game_id TEXT)")));
  QVERIFY(query.exec(QStringLiteral("INSERT INTO ryujinx_games VALUES ('One', '/games/shared.nsp', 'a'), ('Two', '/games/shared.nsp', 'b')")));
  SessionTitleIndex index;
  QVERIFY(index.refresh(database));
  QCOMPARE(index.titleForPath(QStringLiteral("/games/CUSA03173/eboot.bin")), QStringLiteral("Bloodborne"));
  QVERIFY(index.titleForPath(QStringLiteral("/games/other/eboot.bin")).isEmpty());
  // Two titles for one path is no answer at all.
  QVERIFY(index.titleForPath(QStringLiteral("/games/shared.nsp")).isEmpty());
  database.close();
}

void HighlightTests::argumentsDescribeReplayBuffer() {
  ReplayBuffer::Options options;
  options.monitor = QStringLiteral("DP-2");
  options.directory = QStringLiteral("/videos/.recording");
  options.socketPath = QStringLiteral("/run/user/1000/omakade/highlights-1-1.sock");
  options.bufferSeconds = 150;
  const QStringList arguments = ReplayBuffer::arguments(options);
  const auto value = [&arguments](const QString& name) {
    const qsizetype at = arguments.indexOf(name);
    return at >= 0 && at + 1 < arguments.size() ? arguments.at(at + 1) : QString();
  };
  QCOMPARE(value(QStringLiteral("-w")), QStringLiteral("DP-2"));
  QCOMPARE(value(QStringLiteral("-r")), QStringLiteral("150"));
  QCOMPARE(value(QStringLiteral("-replay-storage")), QStringLiteral("ram"));
  QCOMPARE(value(QStringLiteral("-ipc")), options.socketPath);
  QCOMPARE(value(QStringLiteral("-o")), options.directory);
  QCOMPARE(value(QStringLiteral("-c")), QStringLiteral("mp4"));
  QCOMPARE(value(QStringLiteral("-cursor")), QStringLiteral("no"));
  // Each buffer gets its own socket.
  QVERIFY(ReplayBuffer::newSocketPath() != ReplayBuffer::newSocketPath());
}

void HighlightTests::bufferSavesThroughIpc() {
  ReplayBuffer buffer;
  ReplayBuffer::Options options;
  options.program = kFakeRecorder;
  options.monitor = QStringLiteral("DP-1");
  options.directory = QFileInfo(m_config).absolutePath() + QStringLiteral("/recording");
  options.socketPath = ReplayBuffer::newSocketPath();
  buffer.start(options);
  QTRY_COMPARE(buffer.state(), ReplayBuffer::State::Running);
  QString saved, failure;
  int answers = 0;
  buffer.save(30, [&](const QString& path, const QString& error) { saved = path; failure = error; ++answers; });
  QTRY_COMPARE(answers, 1);
  QVERIFY2(failure.isEmpty(), qPrintable(failure));
  QVERIFY(QFileInfo(saved).isFile());
  QCOMPARE(lastRequest(m_log).value(QStringLiteral("name")).toString(), QStringLiteral("save-replay"));
  QCOMPARE(lastRequest(m_log).value(QStringLiteral("data")).toObject().value(QStringLiteral("seconds")).toInt(), 30);
  buffer.stop();
  QCOMPARE(buffer.state(), ReplayBuffer::State::Stopped);
  QTRY_VERIFY(!QFileInfo::exists(options.socketPath));
}

void HighlightTests::bufferReportsRecorderCrash() {
  qputenv("FAKE_GSR_MODE", "crash");
  ReplayBuffer buffer;
  QSignalSpy states(&buffer, &ReplayBuffer::stateChanged);
  ReplayBuffer::Options options;
  options.program = kFakeRecorder;
  options.monitor = QStringLiteral("DP-1");
  options.directory = QFileInfo(m_config).absolutePath() + QStringLiteral("/recording");
  options.socketPath = ReplayBuffer::newSocketPath();
  buffer.start(options);
  QTRY_COMPARE(buffer.state(), ReplayBuffer::State::Failed);
  // The recorder's own words reach the user, not just an exit status.
  QVERIFY2(buffer.lastError().contains(QStringLiteral("failed to capture monitor")), qPrintable(buffer.lastError()));
  int answers = 0;
  QString failure;
  buffer.save(30, [&](const QString&, const QString& error) { failure = error; ++answers; });
  QCOMPARE(answers, 1);
  QVERIFY(!failure.isEmpty());

  ReplayBuffer missing;
  options.program = QStringLiteral("/nonexistent/gpu-screen-recorder");
  missing.start(options);
  QTRY_COMPARE(missing.state(), ReplayBuffer::State::Failed);
  ReplayBuffer none;
  options.program.clear();
  none.start(options);
  QCOMPARE(none.state(), ReplayBuffer::State::Failed);
  QVERIFY(none.lastError().contains(QStringLiteral("not installed")));
}

void HighlightTests::bufferGivesUpWithoutSocket() {
  qputenv("FAKE_GSR_MODE", "nosocket");
  ReplayBuffer buffer;
  buffer.startTimeoutMs = 600;
  ReplayBuffer::Options options;
  options.program = kFakeRecorder;
  options.monitor = QStringLiteral("DP-1");
  options.directory = QFileInfo(m_config).absolutePath() + QStringLiteral("/recording");
  options.socketPath = ReplayBuffer::newSocketPath();
  int answers = 0;
  QString failure;
  buffer.start(options);
  // A save asked for while starting waits for the start, and is answered when it fails.
  buffer.save(30, [&](const QString&, const QString& error) { failure = error; ++answers; });
  QTRY_COMPARE(buffer.state(), ReplayBuffer::State::Failed);
  QVERIFY(buffer.lastError().contains(QStringLiteral("did not start")));
  QTRY_COMPARE(answers, 1);
  QVERIFY(!failure.isEmpty());
}

void HighlightTests::saveFailureIsReported() {
  qputenv("FAKE_GSR_MODE", "fail-save");
  ReplayBuffer buffer;
  ReplayBuffer::Options options;
  options.program = kFakeRecorder;
  options.monitor = QStringLiteral("DP-1");
  options.directory = QFileInfo(m_config).absolutePath() + QStringLiteral("/recording");
  options.socketPath = ReplayBuffer::newSocketPath();
  buffer.start(options);
  QTRY_COMPARE(buffer.state(), ReplayBuffer::State::Running);
  QString saved = QStringLiteral("unset"), failure;
  buffer.save(30, [&](const QString& path, const QString& error) { saved = path; failure = error; });
  QTRY_COMPARE(failure, QStringLiteral("replay buffer is empty"));
  QVERIFY(saved.isEmpty());
  // The buffer keeps running after a refused save, and later saves still go through.
  QCOMPARE(buffer.state(), ReplayBuffer::State::Running);
}

void HighlightTests::stopAnswersPendingSaveOnce() {
  qputenv("FAKE_GSR_MODE", "slow");
  ReplayBuffer buffer;
  ReplayBuffer::Options options;
  options.program = kFakeRecorder;
  options.monitor = QStringLiteral("DP-1");
  options.directory = QFileInfo(m_config).absolutePath() + QStringLiteral("/recording");
  options.socketPath = ReplayBuffer::newSocketPath();
  buffer.start(options);
  QTRY_COMPARE(buffer.state(), ReplayBuffer::State::Running);
  int first = 0, second = 0;
  buffer.save(30, [&](const QString&, const QString&) { ++first; });
  buffer.save(30, [&](const QString&, const QString&) { ++second; });
  QTRY_VERIFY(lastRequest(m_log).contains(QStringLiteral("name")));
  buffer.stop();
  QCOMPARE(first, 1);
  QCOMPARE(second, 1);
  // Nothing late from the stopped recorder answers either save a second time.
  QTest::qWait(300);
  QCOMPARE(first, 1);
  QCOMPARE(second, 1);
}

void HighlightTests::serviceFollowsGameAndMonitor() {
  HighlightService service(environment());
  HighlightService::Game game;
  game.pid = 100;
  game.source = QStringLiteral("Steam");
  game.key = QStringLiteral("367520");
  game.title = QStringLiteral("Hollow Knight");
  // No output yet: the game's window has not appeared, so there is nothing to record.
  service.follow(game);
  QCOMPARE(service.buffer().state(), ReplayBuffer::State::Stopped);
  game.monitor = QStringLiteral("DP-1");
  service.follow(game);
  QTRY_VERIFY(service.running());
  QCOMPARE(service.guideStatus().value(QStringLiteral("seconds")).toInt(), 30);
  QCOMPARE(service.buffer().options().monitor, QStringLiteral("DP-1"));
  // The same game moved to the TV: the buffer follows it.
  game.monitor = QStringLiteral("HDMI-A-1");
  service.follow(game);
  QTRY_VERIFY(service.running());
  QCOMPARE(service.buffer().options().monitor, QStringLiteral("HDMI-A-1"));
  QCOMPARE(starts(m_log), 2);
  // A snapshot with the same game and output changes nothing.
  service.follow(game);
  QTest::qWait(200);
  QCOMPARE(starts(m_log), 2);
  // The game ends.
  m_alive.clear();
  service.follow({});
  QCOMPARE(service.buffer().state(), ReplayBuffer::State::Stopped);
  QVERIFY(service.guideStatus().isEmpty());
}

void HighlightTests::serviceKeepsBufferForHiddenGame() {
  HighlightService service(environment());
  HighlightService::Game game;
  game.pid = 100;
  game.source = QStringLiteral("Steam");
  game.key = QStringLiteral("1");
  game.monitor = QStringLiteral("DP-1");
  service.follow(game);
  QTRY_VERIFY(service.running());
  // A Steam game behind another window drops out of the snapshot without ending.
  service.follow({});
  QVERIFY(service.running());
  QCOMPARE(starts(m_log), 1);
  // A different game in view replaces it.
  m_alive.insert(200);
  HighlightService::Game other = game;
  other.pid = 200;
  service.follow(other);
  QTRY_VERIFY(service.running());
  QCOMPARE(service.game().pid, qint64(200));
  QCOMPARE(starts(m_log), 2);
}

void HighlightTests::serviceStopsWhenTurnedOff() {
  HighlightService service(environment());
  HighlightService::Game game;
  game.pid = 100;
  game.monitor = QStringLiteral("DP-1");
  service.follow(game);
  QTRY_VERIFY(service.running());
  writeConfig(QStringLiteral("highlights = false\n"));
  service.reconcile();
  QCOMPARE(service.buffer().state(), ReplayBuffer::State::Stopped);
  // A longer clip needs a longer buffer, so changing it restarts the recorder.
  writeConfig(QStringLiteral("highlights = true\nhighlight_seconds = 120\n"));
  service.reconcile();
  QTRY_VERIFY(service.running());
  QCOMPARE(service.buffer().options().bufferSeconds, 150);
}

void HighlightTests::serviceFilesClipUnderTitle() {
  HighlightService service(environment());
  QSignalSpy saved(&service, &HighlightService::clipSaved);
  HighlightService::Game game;
  game.pid = 100;
  game.source = QStringLiteral("Steam");
  game.key = QStringLiteral("367520");
  game.title = QStringLiteral("Hollow Knight");
  game.monitor = QStringLiteral("DP-1");
  service.follow(game);
  QTRY_VERIFY(service.running());
  ClipLibrary::Clip result;
  QString failure = QStringLiteral("unset");
  service.clip(QStringLiteral("button"), {}, [&](const ClipLibrary::Clip& clip, const QString& error) {
    result = clip;
    failure = error;
  });
  QTRY_VERIFY(failure != QStringLiteral("unset"));
  QVERIFY2(failure.isEmpty(), qPrintable(failure));
  QVERIFY(result.id > 0);
  QCOMPARE(QFileInfo(result.path).dir().dirName(), QStringLiteral("Hollow Knight"));
  QCOMPARE(result.seconds, 30);
  QCOMPARE(saved.count(), 1);
  QVERIFY(m_notifications.last().startsWith(QStringLiteral("Clip saved")));
  // The recorder's scratch folder is left empty.
  QVERIFY(QDir(QFileInfo(result.path).dir().absolutePath() + QStringLiteral("/../.recording"))
              .entryList(QDir::Files).isEmpty());
}

void HighlightTests::serviceRefusesWithReason() {
  writeConfig(QStringLiteral("highlights = false\n"));
  HighlightService service(environment());
  QString failure;
  service.clip(QStringLiteral("button"), {}, [&](const ClipLibrary::Clip&, const QString& error) { failure = error; });
  QVERIFY(failure.contains(QStringLiteral("Highlights are off")));
  QVERIFY(m_notifications.last().startsWith(QStringLiteral("Clip not saved")));
  writeConfig(QStringLiteral("highlights = true\n"));
  QVERIFY(service.unavailableReason().contains(QStringLiteral("No game")));
}

void HighlightTests::serviceRetriesThenReportsOnce() {
  qputenv("FAKE_GSR_MODE", "crash");
  HighlightService service(environment());
  HighlightService::Game game;
  game.pid = 100;
  game.monitor = QStringLiteral("DP-1");
  service.follow(game);
  // Three attempts, five and ten seconds apart, then one notification and no more tries.
  QTRY_COMPARE_WITH_TIMEOUT(starts(m_log), 3, 20000);
  QTRY_COMPARE(m_notifications.count(), 1);
  QVERIFY(m_notifications.first().startsWith(QStringLiteral("Highlights stopped")));
  service.reconcile();
  QTest::qWait(300);
  QCOMPARE(starts(m_log), 3);
  QVERIFY(service.unavailableReason().contains(QStringLiteral("failed to capture monitor")));
}

void HighlightTests::serviceTrimsGuideTime() {
  const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty() || QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
    QSKIP("ffmpeg is not installed");
  const QString video = QFileInfo(m_config).absolutePath() + QStringLiteral("/source.mp4");
  QProcess make;
  make.start(ffmpeg, {QStringLiteral("-nostdin"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("testsrc=size=160x90:rate=30:duration=8"), QStringLiteral("-g"), QStringLiteral("30"),
                      QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-y"), video});
  QVERIFY(make.waitForFinished(30000));
  QCOMPARE(make.exitCode(), 0);
  qputenv("FAKE_GSR_VIDEO", video.toUtf8());

  HighlightService service(environment());
  HighlightService::Game game;
  game.pid = 100;
  game.title = QStringLiteral("Test Pattern");
  game.monitor = QStringLiteral("DP-1");
  service.follow(game);
  QTRY_VERIFY(service.running());
  ClipLibrary::Clip result;
  bool answered = false;
  service.clip(QStringLiteral("button"), {}, [&](const ClipLibrary::Clip& clip, const QString&) {
    result = clip;
    answered = true;
  }, 3000);
  QTRY_VERIFY_WITH_TIMEOUT(answered, 20000);
  QVERIFY(QFileInfo(result.path).isFile());
  // The extra seconds were asked for, so the clip still holds a full length before the guide.
  QCOMPARE(lastRequest(m_log).value(QStringLiteral("data")).toObject().value(QStringLiteral("seconds")).toInt(), 33);
  QProcess probe;
  probe.start(QStandardPaths::findExecutable(QStringLiteral("ffprobe")),
              {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
               QStringLiteral("format=duration"), QStringLiteral("-of"), QStringLiteral("csv=p=0"), result.path});
  QVERIFY(probe.waitForFinished(10000));
  const double duration = probe.readAllStandardOutput().trimmed().toDouble();
  QVERIFY2(duration > 4.5 && duration < 5.5, qPrintable(QString::number(duration)));
}

QTEST_GUILESS_MAIN(HighlightTests)
#include "HighlightTests.moc"
