#include "ask/AskAgent.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace AskAgent {
namespace {

struct Known {
  const char* id;
  const char* name;
  bool supported;
  bool canLookUp;
};

// Every agent `omarchy default agent` offers. Codex and Claude Code take a picture with the
// question and can be held to no tools that read files; the others can't be driven that way
// yet. Codex's web search needs its code-mode host, which also runs JavaScript that can
// read any file, so Codex answers without the web.
constexpr Known kKnown[] = {
    {"codex", "Codex", true, false},         {"claude", "Claude Code", true, true},
    {"opencode", "OpenCode", false, false},  {"pi", "Pi", false, false},
    {"omp", "Oh My Pi", false, false},       {"ori", "Ori", false, false},
    {"crush", "Crush", false, false},        {"grok", "Grok", false, false},
    {"agy", "Antigravity", false, false},    {"copilot", "GitHub Copilot", false, false},
};

// The longest answer the card shows.
constexpr int kLongest = 420;

QJsonObject object(const QByteArray& line) {
  return QJsonDocument::fromJson(line.trimmed()).object();
}

Reply readCodex(const QByteArray& output) {
  Reply reply;
  for (const auto& line : output.split('\n')) {
    const auto event = object(line);
    const auto type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("item.completed")) {
      const auto item = event.value(QStringLiteral("item")).toObject();
      // A search turn says what it is about to do first; the last message is the answer.
      if (item.value(QStringLiteral("type")) == QStringLiteral("agent_message"))
        reply.answer = item.value(QStringLiteral("text")).toString();
    } else if (type == QStringLiteral("turn.completed")) {
      reply.done = true;
    } else if (type == QStringLiteral("turn.failed")) {
      reply.done = true;
      reply.error = event.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
    } else if (type == QStringLiteral("error")) {
      reply.error = event.value(QStringLiteral("message")).toString();
    }
  }
  return reply;
}

Reply readClaude(const QByteArray& output) {
  Reply reply;
  for (const auto& line : output.split('\n')) {
    const auto event = object(line);
    if (event.value(QStringLiteral("type")) != QStringLiteral("result")) continue;
    reply.done = true;
    const auto text = event.value(QStringLiteral("result")).toString();
    if (event.value(QStringLiteral("is_error")).toBool() ||
        event.value(QStringLiteral("subtype")).toString() != QStringLiteral("success"))
      reply.error = text.isEmpty() ? event.value(QStringLiteral("subtype")).toString() : text;
    else
      reply.answer = text;
  }
  return reply;
}

} // namespace

Agent named(const QString& id) {
  if (id.isEmpty()) return {};
  for (const auto& known : kKnown)
    if (id == QLatin1String(known.id)) return {id, QString::fromLatin1(known.name), known.supported, known.canLookUp};
  return {id, id, false, false};
}

Agent defaultAgent(const QString& home) {
  // omarchy-default-agent keeps its choice here, under $HOME/.config whatever XDG says.
  QFile file(home + QStringLiteral("/.config/omarchy/defaults/agent"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  const auto id = QString::fromUtf8(file.readLine(256)).trimmed();
  static const QRegularExpression name(QStringLiteral("^[a-z0-9-]{1,32}$"));
  return name.match(id).hasMatch() ? named(id) : Agent{};
}

bool enabled(const QString& configContents) {
  static const QRegularExpression pattern(QStringLiteral("(?m)^ask\\s*=\\s*(true|false)\\s*$"));
  const auto match = pattern.match(configContents);
  return match.hasMatch() && match.captured(1) == QStringLiteral("true");
}

bool enabledIn(const QString& configPath) {
  QFile file(configPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
  return enabled(QString::fromUtf8(file.read(1024 * 1024)));
}

QString prompt(const Question& question) {
  auto game = question.title.trimmed().isEmpty() ? QStringLiteral("a game") : question.title.trimmed();
  if (!question.platform.trimmed().isEmpty()) game += QStringLiteral(" (%1)").arg(question.platform.trimmed());
  QStringList lines{
      QStringLiteral("You are the in-game helper in Omakade, a game launcher. The player paused %1 and asked "
                     "what to do now. The attached image is a screenshot of the paused game.").arg(game),
      QStringLiteral("Work out from the screenshot where they are and what they are doing, then tell them the "
                     "next step: where to go, or how to get past what is in front of them."),
      QStringLiteral("No spoilers: never mention story events, characters, bosses or items they have not "
                     "reached yet.")};
  if (question.mode == Mode::Next) {
    lines << QStringLiteral("Answer from what you already know about the game, without using tools.")
          << QStringLiteral("Reply with at most two short sentences of plain text. No preamble and no markdown.");
  } else {
    const auto earlier = plain(question.earlier);
    if (!earlier.isEmpty())
      lines << QStringLiteral("Your earlier answer was: \"%1\" Check it and give the next step with the detail they "
                              "need, without mentioning the earlier answer.").arg(earlier);
    lines << QStringLiteral("Search the web for a guide to this part of the game before you answer.")
          << QStringLiteral("Reply with at most three short sentences of plain text, then the guide site's name in "
                            "parentheses. No preamble and no markdown.");
  }
  return lines.join(QLatin1Char(' '));
}

Command command(const Agent& agent, const QString& program, const QString& workDir, const Question& question) {
  if (!agent.supported || program.isEmpty() || question.imagePath.isEmpty()) return {};
  const bool lookup = question.mode == Mode::Lookup;
  if (lookup && !agent.canLookUp) return {};
  const auto text = prompt(question);
  if (agent.id == QStringLiteral("codex")) {
    // One ephemeral turn in an empty folder, in the read-only sandbox, without the web. The
    // sandbox stops writes but not reads, so everything that could read a file goes too: the
    // shell and its successor, the code-mode host and plugins (each brings a JavaScript
    // runner), the image viewer, sub-agents, skills, apps, the browser, computer use and MCP
    // servers. Unknown names in a Codex without them are ignored.
    QStringList arguments{QStringLiteral("exec"), QStringLiteral("--json"), QStringLiteral("--skip-git-repo-check"),
                          QStringLiteral("--ephemeral"), QStringLiteral("--sandbox"), QStringLiteral("read-only"),
                          QStringLiteral("-C"), workDir,
                          QStringLiteral("-c"), QStringLiteral("model_reasoning_effort=\"low\""),
                          QStringLiteral("-c"), QStringLiteral("mcp_servers={}"),
                          QStringLiteral("-c"), QStringLiteral("web_search=\"disabled\"")};
    for (const auto* feature : {"shell_tool", "unified_exec", "code_mode_host", "plugins", "view_image", "multi_agent",
                                "skill_search", "apps", "browser_use", "computer_use", "image_generation", "memories",
                                "goals", "sleep_tool"})
      arguments << QStringLiteral("--disable") << QString::fromLatin1(feature);
    arguments << QStringLiteral("--image=") + question.imagePath << QStringLiteral("--") << text;
    return {program, arguments, {}};
  }
  if (agent.id == QStringLiteral("claude")) {
    QFile image(question.imagePath);
    if (!image.open(QIODevice::ReadOnly)) return {};
    const auto bytes = image.readAll();
    if (bytes.isEmpty()) return {};
    const auto media = question.imagePath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
                           ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
    const QJsonObject message{
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("message"),
         QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                     {QStringLiteral("content"),
                      QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                                             {QStringLiteral("source"),
                                              QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                                                          {QStringLiteral("media_type"), media},
                                                          {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())}}}},
                                 QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                             {QStringLiteral("text"), text}}}}}}};
    // The picture comes with the question, so no tool is needed to read it. No MCP servers,
    // hooks or settings, which also keeps the start fast; the web only when looking it up.
    const auto tools = lookup ? QStringLiteral("WebSearch,WebFetch") : QString();
    QStringList arguments{QStringLiteral("-p"), QStringLiteral("--input-format"), QStringLiteral("stream-json"),
                          QStringLiteral("--output-format"), QStringLiteral("stream-json"), QStringLiteral("--verbose"),
                          QStringLiteral("--no-session-persistence"), QStringLiteral("--strict-mcp-config"),
                          QStringLiteral("--setting-sources"), QString(), QStringLiteral("--effort"), QStringLiteral("low"),
                          QStringLiteral("--tools"), tools};
    if (lookup) arguments << QStringLiteral("--allowedTools") << tools;
    return {program, arguments, QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'};
  }
  return {};
}

Reply read(const Agent& agent, const QByteArray& output) {
  auto reply = agent.id == QStringLiteral("codex") ? readCodex(output)
               : agent.id == QStringLiteral("claude") ? readClaude(output) : Reply{};
  reply.answer = plain(reply.answer);
  reply.error = reply.error.simplified();
  return reply;
}

QString plain(const QString& text) {
  static const QRegularExpression link(QStringLiteral("\\[([^\\]\\n]+)\\]\\([^)\\s]+\\)"));
  static const QRegularExpression url(QStringLiteral("<?https?://[^\\s)>]+>?"));
  static const QRegularExpression emptyBrackets(QStringLiteral("\\(\\s*\\)"));
  static const QRegularExpression marks(QStringLiteral("\\*\\*|__|`|^#+\\s+|^\\s*[-*]\\s+"),
                                        QRegularExpression::MultilineOption);
  static const QRegularExpression beforePunctuation(QStringLiteral("\\s+([.,;:!?)])"));
  static const QRegularExpression danglingComma(QStringLiteral("[,;:]+\\)"));
  auto result = text;
  result.replace(link, QStringLiteral("\\1"));
  result.replace(url, QString());
  result.replace(marks, QString());
  result.replace(emptyBrackets, QString());
  result = result.simplified();
  result.replace(beforePunctuation, QStringLiteral("\\1"));
  result.replace(danglingComma, QStringLiteral(")"));
  if (result.size() <= kLongest) return result;
  // End at the last sentence that fits, or cut at a word.
  for (int at = kLongest - 1; at > kLongest / 2; --at)
    if (QStringLiteral(".!?").contains(result.at(at)) && (at + 1 == result.size() || result.at(at + 1) == QLatin1Char(' ')))
      return result.left(at + 1);
  const int space = result.lastIndexOf(QLatin1Char(' '), kLongest - 1);
  return result.left(space > 0 ? space : kLongest - 1) + QChar(0x2026);
}

} // namespace AskAgent
