#include "ask/AskAgent.h"
#include "ask/AskService.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// Stand-ins for the agents, from the fixtures. FAKE_ASK picks what they do; each logs its
// arguments, one per line, and what it was given on its input.
const QString kFakeCodex = QStringLiteral(OMAKADE_FIXTURE_DIR "/fake-ask-codex");
const QString kFakeClaude = QStringLiteral(OMAKADE_FIXTURE_DIR "/fake-ask-claude");

void write(const QString& path, const QByteArray& contents) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(contents);
}

QStringList lines(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::KeepEmptyParts);
}

} // namespace

class AskTests final : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void defaultAgent();
  void settingIsOffUnlessOn();
  void promptCarriesGameAndRules();
  void codexCommand();
  void claudeCommand();
  void unsupportedAgentHasNoCommand();
  void readsCodex();
  void readsClaude();
  void plainText();

  void statusNeedsAskAgentAndGame();
  void answersAndLooksItUp();
  void claudeGetsThePicture();
  void answerDoesNotWaitForExit();
  void takesOnlyItsOwnPictures();
  void oneQuestionAtATime();
  void failuresSayWhy();
  void slowAgentIsStopped();
  void missingAgent();
  void anotherGameStartsOver();
  void endedGameLeavesNothing();
  void answeredAgentIsStoppedLater();
  void turningOffStopsTheQuestion();

private:
  std::unique_ptr<QTemporaryDir> m_root;
  AskService::Environment environment(const QString& mode = QStringLiteral("answer")) const;
  void setAgent(const QString& id) const;
  void setEnabled(bool on) const;
  QString shot(const AskService& service, const QString& name = QStringLiteral("shot.jpg")) const;
  QString log() const { return m_root->filePath(QStringLiteral("log")); }
  static AskService::Game game(qint64 pid = 100) { return {pid, 7, QStringLiteral("Ratchet & Clank"), QStringLiteral("PS4")}; }
};

void AskTests::init() {
  m_root = std::make_unique<QTemporaryDir>();
  QVERIFY(m_root->isValid());
  QVERIFY(QDir().mkpath(m_root->filePath(QStringLiteral("bin"))));
  QVERIFY(QFile::copy(kFakeCodex, m_root->filePath(QStringLiteral("bin/codex"))));
  QVERIFY(QFile::copy(kFakeClaude, m_root->filePath(QStringLiteral("bin/claude"))));
  setAgent(QStringLiteral("codex"));
  setEnabled(true);
}

void AskTests::cleanup() { m_root.reset(); }

AskService::Environment AskTests::environment(const QString& mode) const {
  AskService::Environment environment;
  environment.home = m_root->filePath(QStringLiteral("home"));
  environment.configPath = m_root->filePath(QStringLiteral("config.toml"));
  environment.folder = m_root->filePath(QStringLiteral("run/ask"));
  environment.process = QProcessEnvironment::systemEnvironment();
  environment.process.insert(QStringLiteral("PATH"), m_root->filePath(QStringLiteral("bin")) + QStringLiteral(":/usr/bin:/bin"));
  environment.process.insert(QStringLiteral("FAKE_ASK"), mode);
  environment.process.insert(QStringLiteral("FAKE_ASK_LOG"), log());
  environment.now = [] { return qint64(1000); };
  environment.timeoutMs = 5000;
  return environment;
}

void AskTests::setAgent(const QString& id) const {
  const auto path = m_root->filePath(QStringLiteral("home/.config/omarchy/defaults/agent"));
  if (id.isEmpty()) QFile::remove(path);
  else write(path, id.toUtf8() + '\n');
}

void AskTests::setEnabled(bool on) const {
  write(m_root->filePath(QStringLiteral("config.toml")),
        QByteArray("highlights = true\nask = ") + (on ? "true" : "false") + "\n");
}

QString AskTests::shot(const AskService& service, const QString& name) const {
  const auto path = service.shotFolder() + QLatin1Char('/') + name;
  write(path, QByteArray("\xff\xd8 not really a jpeg", 20));
  return path;
}

void AskTests::defaultAgent() {
  const auto home = m_root->filePath(QStringLiteral("home"));
  const auto agent = AskAgent::defaultAgent(home);
  QCOMPARE(agent.id, QStringLiteral("codex"));
  QCOMPARE(agent.name, QStringLiteral("Codex"));
  QVERIFY(agent.supported);
  setAgent(QStringLiteral("claude"));
  QCOMPARE(AskAgent::defaultAgent(home).name, QStringLiteral("Claude Code"));
  QVERIFY(AskAgent::defaultAgent(home).supported);
  // Agents Ask can't drive yet keep their names, so settings can say which it is.
  setAgent(QStringLiteral("opencode"));
  QCOMPARE(AskAgent::defaultAgent(home).name, QStringLiteral("OpenCode"));
  QVERIFY(!AskAgent::defaultAgent(home).supported);
  setAgent(QStringLiteral("something-new"));
  QCOMPARE(AskAgent::defaultAgent(home).name, QStringLiteral("something-new"));
  QVERIFY(!AskAgent::defaultAgent(home).supported);
  setAgent(QStringLiteral("../../bin/sh"));
  QVERIFY(!AskAgent::defaultAgent(home).valid());
  // Omarchy chooses no agent until the user does.
  setAgent({});
  QVERIFY(!AskAgent::defaultAgent(home).valid());
}

void AskTests::settingIsOffUnlessOn() {
  QVERIFY(AskAgent::enabled(QStringLiteral("ask = true\n")));
  QVERIFY(AskAgent::enabled(QStringLiteral("highlights = false\nask=true")));
  QVERIFY(!AskAgent::enabled(QStringLiteral("ask = false\n")));
  QVERIFY(!AskAgent::enabled(QStringLiteral("task = true\n")));
  QVERIFY(!AskAgent::enabled(QStringLiteral("# ask = true\n")));
  QVERIFY(!AskAgent::enabled({}));
  QVERIFY(!AskAgent::enabledIn(m_root->filePath(QStringLiteral("missing.toml"))));
}

void AskTests::promptCarriesGameAndRules() {
  AskAgent::Question question{QStringLiteral("Ratchet & Clank"), QStringLiteral("PS4"), AskAgent::Mode::Next, QStringLiteral("/x.jpg"), {}};
  const auto next = AskAgent::prompt(question);
  QVERIFY(next.contains(QStringLiteral("Ratchet & Clank (PS4)")));
  QVERIFY(next.contains(QStringLiteral("No spoilers")));
  QVERIFY(next.contains(QStringLiteral("without using tools")));
  QVERIFY(next.contains(QStringLiteral("at most two short sentences")));
  QVERIFY(!next.contains(QStringLiteral("Search the web")));
  question.mode = AskAgent::Mode::Lookup;
  question.earlier = QStringLiteral("Talk to **Grim**.");
  const auto lookup = AskAgent::prompt(question);
  QVERIFY(lookup.contains(QStringLiteral("Your earlier answer was: \"Talk to Grim.\"")));
  QVERIFY(lookup.contains(QStringLiteral("Search the web")));
  QVERIFY(lookup.contains(QStringLiteral("No spoilers")));
  QVERIFY(AskAgent::prompt({}).contains(QStringLiteral("paused a game and")));
}

void AskTests::codexCommand() {
  const auto agent = AskAgent::named(QStringLiteral("codex"));
  AskAgent::Question question{QStringLiteral("Lantern Road"), {}, AskAgent::Mode::Next, QStringLiteral("/run/shots/a.jpg"), {}};
  auto command = AskAgent::command(agent, QStringLiteral("/bin/codex"), QStringLiteral("/run/work"), question);
  QCOMPARE(command.program, QStringLiteral("/bin/codex"));
  const auto& a = command.arguments;
  QCOMPARE(a.first(), QStringLiteral("exec"));
  QVERIFY(a.contains(QStringLiteral("--json")));
  QVERIFY(a.contains(QStringLiteral("--ephemeral")));
  QCOMPARE(a.at(a.indexOf(QStringLiteral("--sandbox")) + 1), QStringLiteral("read-only"));
  QCOMPARE(a.at(a.indexOf(QStringLiteral("-C")) + 1), QStringLiteral("/run/work"));
  QVERIFY(a.contains(QStringLiteral("mcp_servers={}")));
  QVERIFY(a.contains(QStringLiteral("web_search=\"disabled\"")));
  QVERIFY(a.contains(QStringLiteral("model_reasoning_effort=\"low\"")));
  QVERIFY(a.contains(QStringLiteral("--image=/run/shots/a.jpg")));
  // The read-only sandbox still lets tools read files, so every tool that can is turned off.
  QStringList disabled;
  for (int i = 0; i + 1 < a.size(); ++i)
    if (a.at(i) == QStringLiteral("--disable")) disabled << a.at(i + 1);
  for (const auto& feature : {"shell_tool", "unified_exec", "code_mode_host", "plugins", "view_image", "multi_agent", "skill_search", "apps"})
    QVERIFY2(disabled.contains(QString::fromLatin1(feature)), feature);
  // The question is last, after an end of options.
  QCOMPARE(a.at(a.size() - 2), QStringLiteral("--"));
  QCOMPARE(a.last(), AskAgent::prompt(question));
  QVERIFY(command.input.isEmpty());
  // Codex's web search needs its code-mode host, which can read files: no Look it up.
  QVERIFY(!agent.canLookUp);
  question.mode = AskAgent::Mode::Lookup;
  QVERIFY(AskAgent::command(agent, QStringLiteral("/bin/codex"), QStringLiteral("/run/work"), question).program.isEmpty());
}

void AskTests::claudeCommand() {
  const auto image = m_root->filePath(QStringLiteral("a.png"));
  write(image, "png bytes");
  const auto agent = AskAgent::named(QStringLiteral("claude"));
  AskAgent::Question question{QStringLiteral("Lantern Road"), {}, AskAgent::Mode::Next, image, {}};
  auto command = AskAgent::command(agent, QStringLiteral("/bin/claude"), QStringLiteral("/run/work"), question);
  const auto& a = command.arguments;
  QCOMPARE(a.first(), QStringLiteral("-p"));
  QVERIFY(a.contains(QStringLiteral("--no-session-persistence")));
  QVERIFY(a.contains(QStringLiteral("--strict-mcp-config")));
  QCOMPARE(a.at(a.indexOf(QStringLiteral("--setting-sources")) + 1), QString());
  // No tools at all for the first answer.
  QCOMPARE(a.at(a.indexOf(QStringLiteral("--tools")) + 1), QString());
  QVERIFY(!a.contains(QStringLiteral("--allowedTools")));
  const auto message = QJsonDocument::fromJson(command.input).object();
  const auto content = message.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toArray();
  QCOMPARE(content.size(), 2);
  const auto source = content.at(0).toObject().value(QStringLiteral("source")).toObject();
  QCOMPARE(source.value(QStringLiteral("media_type")).toString(), QStringLiteral("image/png"));
  QCOMPARE(QByteArray::fromBase64(source.value(QStringLiteral("data")).toString().toLatin1()), QByteArray("png bytes"));
  QCOMPARE(content.at(1).toObject().value(QStringLiteral("text")).toString(), AskAgent::prompt(question));
  QVERIFY(command.input.endsWith('\n'));
  question.mode = AskAgent::Mode::Lookup;
  command = AskAgent::command(agent, QStringLiteral("/bin/claude"), QStringLiteral("/run/work"), question);
  QCOMPARE(command.arguments.at(command.arguments.indexOf(QStringLiteral("--tools")) + 1), QStringLiteral("WebSearch,WebFetch"));
  QCOMPARE(command.arguments.at(command.arguments.indexOf(QStringLiteral("--allowedTools")) + 1), QStringLiteral("WebSearch,WebFetch"));
  // A picture that can't be read asks nothing.
  question.imagePath = m_root->filePath(QStringLiteral("missing.png"));
  QVERIFY(AskAgent::command(agent, QStringLiteral("/bin/claude"), {}, question).program.isEmpty());
}

void AskTests::unsupportedAgentHasNoCommand() {
  const AskAgent::Question question{QStringLiteral("Lantern Road"), {}, AskAgent::Mode::Next, QStringLiteral("/a.jpg"), {}};
  QVERIFY(AskAgent::command(AskAgent::named(QStringLiteral("opencode")), QStringLiteral("/bin/opencode"), {}, question).program.isEmpty());
  QVERIFY(AskAgent::command(AskAgent::named(QStringLiteral("codex")), {}, {}, question).program.isEmpty());
}

void AskTests::readsCodex() {
  const auto codex = AskAgent::named(QStringLiteral("codex"));
  QByteArray output = R"({"type":"thread.started"}
{"type":"item.completed","item":{"type":"agent_message","text":"I'll check a guide."}}
{"type":"item.completed","item":{"type":"web_search","query":"x"}}
{"type":"item.completed","item":{"type":"agent_message","text":"Take the lift."}}
)";
  auto reply = AskAgent::read(codex, output);
  QVERIFY(!reply.done);
  QCOMPARE(reply.answer, QStringLiteral("Take the lift."));
  output += "{\"type\":\"turn.completed\"}\n";
  QVERIFY(AskAgent::read(codex, output).done);
  // A half-written line is read once the rest arrives.
  QVERIFY(!AskAgent::read(codex, "{\"type\":\"turn.compl").done);
  reply = AskAgent::read(codex, "{\"type\":\"turn.failed\",\"error\":{\"message\":\"usage limit\\nreached\"}}\n");
  QVERIFY(reply.done);
  QCOMPARE(reply.error, QStringLiteral("usage limit reached"));
  QVERIFY(reply.answer.isEmpty());
}

void AskTests::readsClaude() {
  const auto claude = AskAgent::named(QStringLiteral("claude"));
  auto reply = AskAgent::read(claude, R"({"type":"system"}
{"type":"result","subtype":"success","is_error":false,"result":"Head **out**."}
)");
  QVERIFY(reply.done);
  QCOMPARE(reply.answer, QStringLiteral("Head out."));
  reply = AskAgent::read(claude, R"({"type":"result","subtype":"success","is_error":true,"result":"Not logged in"})");
  QVERIFY(reply.done);
  QVERIFY(reply.answer.isEmpty());
  QCOMPARE(reply.error, QStringLiteral("Not logged in"));
  reply = AskAgent::read(claude, R"({"type":"result","subtype":"error_max_turns","is_error":false})");
  QCOMPARE(reply.error, QStringLiteral("error_max_turns"));
  QVERIFY(!AskAgent::read(claude, R"({"type":"assistant"})").done);
}

void AskTests::plainText() {
  QCOMPARE(AskAgent::plain(QStringLiteral("Press **Triangle** at the `lift` ([Fextralife](https://x.example/a_b)).")),
           QStringLiteral("Press Triangle at the lift (Fextralife)."));
  QCOMPARE(AskAgent::plain(QStringLiteral("Go left. See https://example.com/guide for more.")),
           QStringLiteral("Go left. See for more."));
  QCOMPARE(AskAgent::plain(QStringLiteral("## Next\n- Go up\n- Then left  ")), QStringLiteral("Next Go up Then left"));
  QCOMPARE(AskAgent::plain(QStringLiteral("Source (https://example.com).")), QStringLiteral("Source."));
  QCOMPARE(AskAgent::plain(QStringLiteral("Crank it.\n\n(Ratchet & Clank Wiki, https://example.com/wiki/Save_Veldin!)")),
           QStringLiteral("Crank it. (Ratchet & Clank Wiki)"));
  // A long answer ends at its last sentence that fits.
  const auto sentence = QStringLiteral("Walk along the ledge to the lift. ");
  const auto longer = AskAgent::plain(sentence.repeated(20));
  QVERIFY(longer.size() <= 420);
  QVERIFY(longer.endsWith(QStringLiteral("lift.")));
  // With no sentence end in reach, it stops at a word.
  const auto words = AskAgent::plain(QStringLiteral("word ").repeated(200));
  QVERIFY(words.size() <= 421);
  QVERIFY(words.endsWith(QStringLiteral("word…")));
}

void AskTests::statusNeedsAskAgentAndGame() {
  AskService service(environment());
  QVERIFY(service.guideStatus().isEmpty());
  QSignalSpy changed(&service, &AskService::statusChanged);
  service.follow(game());
  QCOMPARE(changed.count(), 1);
  auto status = service.guideStatus();
  QCOMPARE(status.value(QStringLiteral("agent")).toString(), QStringLiteral("Codex"));
  QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
  QCOMPARE(status.value(QStringLiteral("shots")).toString(), service.shotFolder());
  QVERIFY(!status.value(QStringLiteral("lookup")).toBool());
  // The same game again changes nothing.
  service.follow(game());
  QCOMPARE(changed.count(), 1);
  // Off, or an agent Ask can't drive, or none chosen: no Ask on the card.
  setEnabled(false);
  service.follow(game());
  QCOMPARE(changed.count(), 2);
  QVERIFY(service.guideStatus().isEmpty());
  setEnabled(true);
  setAgent(QStringLiteral("opencode"));
  service.follow(game());
  QVERIFY(service.guideStatus().isEmpty());
  setAgent({});
  service.follow(game());
  QVERIFY(service.guideStatus().isEmpty());
  setAgent(QStringLiteral("claude"));
  service.follow(game());
  QCOMPARE(service.guideStatus().value(QStringLiteral("agent")).toString(), QStringLiteral("Claude Code"));
  // A snapshot that names no game keeps the one Ask follows.
  service.follow({});
  QVERIFY(!service.guideStatus().isEmpty());
}

void AskTests::answersAndLooksItUp() {
  AskService service(environment());
  service.follow(game());
  const auto picture = shot(service);
  QVERIFY(service.ask(QStringLiteral("next"), picture));
  auto status = service.guideStatus();
  QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("thinking"));
  QCOMPARE(status.value(QStringLiteral("started")).toInteger(), 1000);
  QTRY_COMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  status = service.guideStatus();
  QCOMPARE(status.value(QStringLiteral("text")).toString(), QStringLiteral("Walk up to Grim and press Triangle (Guide)."));
  QCOMPARE(status.value(QStringLiteral("mode")).toString(), QStringLiteral("next"));
  QCOMPARE(status.value(QStringLiteral("at")).toInteger(), 1000);
  const auto arguments = lines(log());
  QVERIFY(arguments.contains(QStringLiteral("--image=") + QFileInfo(picture).canonicalFilePath()));
  QVERIFY(arguments.contains(QStringLiteral("web_search=\"disabled\"")));
  QVERIFY(QFileInfo::exists(picture));
  // Codex can't look it up.
  QVERIFY(!status.value(QStringLiteral("lookup")).toBool());
  QVERIFY(!service.ask(QStringLiteral("lookup")));

  // Claude Code can: the same picture again, with web tools and the first answer.
  setAgent(QStringLiteral("claude"));
  AskService claude(environment());
  claude.follow(game());
  const auto first = shot(claude);
  QVERIFY(claude.ask(QStringLiteral("next"), first));
  QTRY_COMPARE(claude.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  QVERIFY(claude.guideStatus().value(QStringLiteral("lookup")).toBool());
  QVERIFY(lines(log()).contains(QStringLiteral("--tools")));
  QCOMPARE(lines(log()).at(lines(log()).indexOf(QStringLiteral("--tools")) + 1), QString());
  QVERIFY(claude.ask(QStringLiteral("lookup")));
  QCOMPARE(claude.guideStatus().value(QStringLiteral("mode")).toString(), QStringLiteral("lookup"));
  QTRY_COMPARE(claude.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  const auto lookup = lines(log());
  QCOMPARE(lookup.at(lookup.indexOf(QStringLiteral("--tools")) + 1), QStringLiteral("WebSearch,WebFetch"));
  QFile input(log() + QStringLiteral(".input"));
  QVERIFY(input.open(QIODevice::ReadOnly));
  QVERIFY(input.readAll().contains("Your earlier answer was: \\\"Head out through the garage door.\\\""));
  QVERIFY(QFileInfo::exists(first));
  // A looked-up answer is not looked up again.
  QVERIFY(!claude.guideStatus().value(QStringLiteral("lookup")).toBool());
  QVERIFY(!claude.ask(QStringLiteral("lookup")));

  // A new question replaces the picture, and any the guide took but never sent.
  const auto stray = shot(claude, QStringLiteral("stray.jpg"));
  const auto second = shot(claude, QStringLiteral("second.jpg"));
  QVERIFY(claude.ask(QStringLiteral("next"), second));
  QVERIFY(!QFileInfo::exists(first));
  QVERIFY(!QFileInfo::exists(stray));
  QTRY_COMPARE(claude.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  QVERIFY(QFileInfo::exists(second));
}

void AskTests::claudeGetsThePicture() {
  setAgent(QStringLiteral("claude"));
  AskService service(environment());
  service.follow(game());
  const auto picture = shot(service);
  QVERIFY(service.ask(QStringLiteral("next"), picture));
  QTRY_COMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  QCOMPARE(service.guideStatus().value(QStringLiteral("text")).toString(), QStringLiteral("Head out through the garage door."));
  QFile input(log() + QStringLiteral(".input"));
  QVERIFY(input.open(QIODevice::ReadOnly));
  const auto message = QJsonDocument::fromJson(input.readAll()).object();
  const auto source = message.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toArray()
                          .at(0).toObject().value(QStringLiteral("source")).toObject();
  QCOMPARE(QByteArray::fromBase64(source.value(QStringLiteral("data")).toString().toLatin1()),
           QByteArray("\xff\xd8 not really a jpeg", 20));
}

void AskTests::answerDoesNotWaitForExit() {
  AskService service(environment(QStringLiteral("linger")));
  service.follow(game());
  QElapsedTimer clock;
  clock.start();
  QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
  QTRY_COMPARE_WITH_TIMEOUT(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"), 3000);
  QVERIFY(clock.elapsed() < 3000);
}

void AskTests::takesOnlyItsOwnPictures() {
  AskService service(environment());
  service.follow(game());
  // Not a picture outside its folder, which is left alone.
  const auto outside = m_root->filePath(QStringLiteral("outside.jpg"));
  write(outside, "jpeg");
  QVERIFY(!service.ask(QStringLiteral("next"), outside));
  QVERIFY(QFileInfo::exists(outside));
  // Nor a link in it, nor one in a folder below it, nor nothing.
  const auto link = service.shotFolder() + QStringLiteral("/link.jpg");
  QVERIFY(QFile::link(outside, link));
  QVERIFY(!service.ask(QStringLiteral("next"), link));
  QVERIFY(!service.ask(QStringLiteral("next"), shot(service, QStringLiteral("deeper/shot.jpg"))));
  QVERIFY(!service.ask(QStringLiteral("next"), service.shotFolder() + QStringLiteral("/missing.jpg")));
  QVERIFY(!service.ask(QStringLiteral("next")));
  QVERIFY(!service.ask(QStringLiteral("other"), shot(service)));
  QVERIFY(QFileInfo::exists(outside));
  QCOMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
  // Nothing to look up before an answer.
  QVERIFY(!service.ask(QStringLiteral("lookup")));
}

void AskTests::oneQuestionAtATime() {
  AskService service(environment(QStringLiteral("hang")));
  service.follow(game());
  QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
  // A second picture while the first is being answered is not kept.
  const auto second = shot(service, QStringLiteral("second.jpg"));
  QVERIFY(!service.ask(QStringLiteral("next"), second));
  QVERIFY(!QFileInfo::exists(second));
  QCOMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("thinking"));
}

void AskTests::failuresSayWhy() {
  {
    AskService service(environment(QStringLiteral("fail")));
    service.follow(game());
    QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
    QTRY_COMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    QCOMPARE(service.guideStatus().value(QStringLiteral("text")).toString(), QStringLiteral("Codex couldn't answer: Error: not logged in"));
    QVERIFY(!service.guideStatus().value(QStringLiteral("lookup")).toBool());
    // A failed question can be asked again.
    QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
  }
  {
    AskService service(environment(QStringLiteral("turnfail")));
    service.follow(game());
    QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
    QTRY_COMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    QCOMPARE(service.guideStatus().value(QStringLiteral("text")).toString(), QStringLiteral("Codex couldn't answer: usage limit reached"));
  }
  {
    AskService service(environment(QStringLiteral("silent")));
    service.follow(game());
    QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
    QTRY_COMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    QCOMPARE(service.guideStatus().value(QStringLiteral("text")).toString(), QStringLiteral("Codex didn't answer."));
  }
}

void AskTests::slowAgentIsStopped() {
  auto settings = environment(QStringLiteral("hang"));
  settings.timeoutMs = 2000;
  AskService service(settings);
  service.follow(game());
  QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
  QTRY_COMPARE_WITH_TIMEOUT(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("failed"), 5000);
  QCOMPARE(service.guideStatus().value(QStringLiteral("text")).toString(), QStringLiteral("Codex didn't answer within 2 seconds."));
  // The agent is gone, not left waiting.
  QTRY_VERIFY_WITH_TIMEOUT(service.findChildren<QProcess*>().isEmpty(), 3000);
}

void AskTests::missingAgent() {
  QFile::remove(m_root->filePath(QStringLiteral("bin/codex")));
  AskService service(environment());
  service.follow(game());
  QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
  QCOMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
  QCOMPARE(service.guideStatus().value(QStringLiteral("text")).toString(), QStringLiteral("Codex isn't installed."));
}

void AskTests::anotherGameStartsOver() {
  AskService service(environment());
  service.follow(game());
  const auto picture = shot(service);
  QVERIFY(service.ask(QStringLiteral("next"), picture));
  QTRY_COMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  // A picture the guide took but never sent goes too.
  const auto unsent = shot(service, QStringLiteral("unsent.jpg"));
  service.follow(game(200));
  QCOMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
  QVERIFY(!service.guideStatus().contains(QStringLiteral("text")));
  QVERIFY(!QFileInfo::exists(picture));
  QVERIFY(!QFileInfo::exists(unsent));
}

void AskTests::endedGameLeavesNothing() {
  auto settings = environment();
  bool running = true;
  settings.alive = [&running](qint64, qint64) { return running; };
  AskService service(settings);
  service.follow(game());
  const auto picture = shot(service);
  QVERIFY(service.ask(QStringLiteral("next"), picture));
  QTRY_COMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  // Out of sight but still running: the answer stays.
  service.follow({});
  QCOMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  QVERIFY(QFileInfo::exists(picture));
  // Ended: nothing about it is kept.
  running = false;
  QSignalSpy changed(&service, &AskService::statusChanged);
  service.follow({});
  QCOMPARE(changed.count(), 1);
  QVERIFY(service.guideStatus().isEmpty());
  QVERIFY(!QFileInfo::exists(picture));
}

void AskTests::answeredAgentIsStoppedLater() {
  auto settings = environment(QStringLiteral("linger"));
  settings.exitGraceMs = 300;
  AskService service(settings);
  service.follow(game());
  QVERIFY(service.ask(QStringLiteral("next"), shot(service)));
  QTRY_COMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("answer"));
  // An agent that keeps running after its answer is stopped once its grace is over.
  QTRY_VERIFY_WITH_TIMEOUT(service.findChildren<QProcess*>().isEmpty(), 4000);
}

void AskTests::turningOffStopsTheQuestion() {
  AskService service(environment(QStringLiteral("hang")));
  service.follow(game());
  const auto picture = shot(service);
  QVERIFY(service.ask(QStringLiteral("next"), picture));
  setEnabled(false);
  service.follow(game());
  QVERIFY(service.guideStatus().isEmpty());
  QVERIFY(!QFileInfo::exists(picture));
  QTRY_VERIFY_WITH_TIMEOUT(service.findChildren<QProcess*>().isEmpty(), 3000);
  // Back on, it starts from nothing.
  setEnabled(true);
  service.follow(game());
  QCOMPARE(service.guideStatus().value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
}

QTEST_GUILESS_MAIN(AskTests)
#include "AskTests.moc"
