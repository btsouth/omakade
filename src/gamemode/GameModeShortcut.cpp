#include "gamemode/GameModeShortcut.h"
#include "gamemode/GameModeDesktop.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrentRun>

namespace {
constexpr int kCommandTimeoutMs = 2500;
// Hyprland's modifier bits.
constexpr int kCtrlMask = 4;
constexpr int kSuperMask = 64;

bool hyprctl(const QStringList& arguments, QByteArray* output = nullptr) {
  const QString executable = QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
  if (executable.isEmpty() || qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()) {
    return false;
  }
  QProcess process;
  process.start(executable, arguments);
  if (!process.waitForStarted(kCommandTimeoutMs)) {
    return false;
  }
  if (!process.waitForFinished(kCommandTimeoutMs)) {
    process.kill();
    process.waitForFinished(kCommandTimeoutMs);
    return false;
  }
  if (output != nullptr) {
    *output = process.readAllStandardOutput();
  }
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QRegularExpression bindingPattern(const GameModeShortcut::Binding& binding) {
  // One uncommented o.bind or o.rebind line whose command is one of the binding's.
  QStringList commands;
  for (const QString& command : binding.commands) commands.append(QRegularExpression::escape(command));
  return QRegularExpression(
      QStringLiteral(R"lua(^[ \t]*o\.(?:re)?bind\([ \t]*"([^"\\\n]+)"[ \t]*,[ \t]*(?:nil|"(?:\\.|[^"\\\n])*")[ \t]*,[ \t]*"(?:%1)"[ \t]*\)[ \t]*(?:--[^\n]*)?$)lua")
          .arg(commands.join(QLatin1Char('|'))),
      QRegularExpression::MultilineOption);
}

bool readFile(const QString& path, QString* contents) {
  // Omarchy creates the file, but one the user removed is simply empty: adding the
  // binding creates it again. A dangling symlink is not, so it is left alone.
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink()) {
    contents->clear();
    return true;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  *contents = QString::fromUtf8(file.readAll());
  return true;
}

bool writeFile(const QString& path, const QString& contents) {
  // A bindings file kept in a dotfiles repository is a symlink; write through it.
  const QFileInfo info(path);
  const bool created = !info.exists() && !info.isSymLink();
  const QString target = created ? info.absoluteFilePath() : info.canonicalFilePath();
  if (target.isEmpty() || (created && !QDir().mkpath(info.absolutePath()))) {
    return false;
  }
  const QFile::Permissions permissions =
      created ? QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther
              : QFile::permissions(target);
  QSaveFile file(target);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(contents.toUtf8());
  if (!file.commit()) {
    return false;
  }
  QFile::setPermissions(target, permissions);
  return true;
}
} // namespace

GameModeShortcut::Binding GameModeShortcut::gameMode() {
  return {.key = QStringLiteral("SUPER + CTRL + G"),
          .description = QStringLiteral("Game Mode"),
          .command = QStringLiteral("omakade --game-mode-toggle"),
          .commands = {QStringLiteral("omakade --game-mode-toggle"), QStringLiteral("omakade --game-mode")},
          .comment = QStringLiteral("-- Omakade Game Mode. Press it again to leave. Added by Omakade."),
          .modmask = kSuperMask | kCtrlMask,
          .keyName = QStringLiteral("G")};
}

GameModeShortcut::Binding GameModeShortcut::clip() {
  return {.key = QStringLiteral("CTRL + PRINT"),
          .description = QStringLiteral("Clip highlight"),
          .command = QStringLiteral("omakade --clip"),
          .commands = {QStringLiteral("omakade --clip")},
          .comment = QStringLiteral("-- Omakade highlights: save the last moments of the game. Added by Omakade."),
          .modmask = kCtrlMask,
          .keyName = QStringLiteral("Print")};
}

QString GameModeShortcut::defaultKey(const Binding& binding) { return binding.key; }

QString GameModeShortcut::command(const Binding& binding) { return binding.command; }

QString GameModeShortcut::bindingLine(const Binding& binding) {
  return QStringLiteral("o.bind(\"%1\", \"%2\", \"%3\")").arg(binding.key, binding.description, binding.command);
}

QString GameModeShortcut::liveBindingScript(const Binding& binding) {
  return QStringLiteral("hl.unbind(\"%1\")\n").arg(binding.key) + bindingLine(binding);
}

QString GameModeShortcut::boundKey(const QString& contents, const Binding& binding) {
  const QRegularExpressionMatch match = bindingPattern(binding).match(contents);
  return match.hasMatch() ? match.captured(1).simplified() : QString{};
}

QString GameModeShortcut::withBinding(const QString& contents, const Binding& binding) {
  if (!boundKey(contents, binding).isEmpty()) {
    return contents;
  }
  QString result = contents;
  if (!result.isEmpty() && !result.endsWith(QLatin1Char('\n'))) {
    result += QLatin1Char('\n');
  }
  if (!result.isEmpty()) {
    result += QLatin1Char('\n');
  }
  return result + binding.comment + QLatin1Char('\n') + bindingLine(binding) + QLatin1Char('\n');
}

QString GameModeShortcut::withoutBinding(const QString& contents, const Binding& binding) {
  QStringList kept;
  const QStringList lines = contents.split(QLatin1Char('\n'));
  const QRegularExpression pattern = bindingPattern(binding);
  for (const QString& line : lines) {
    if (!pattern.match(line).hasMatch()) {
      kept.append(line);
      continue;
    }
    if (!kept.isEmpty() && kept.last().trimmed() == binding.comment) {
      kept.removeLast();
      // The blank line Omakade put above its comment goes too, so adding and removing
      // leaves the file as it was.
      if (kept.size() > 1 && kept.last().trimmed().isEmpty()) {
        kept.removeLast();
      }
    }
  }
  return kept.join(QLatin1Char('\n'));
}

QString GameModeShortcut::removalScript(const QString& contents, const Binding& binding) {
  QStringList keys;
  auto matches = bindingPattern(binding).globalMatch(contents);
  while (matches.hasNext()) {
    const QString key = matches.next().captured(1).simplified();
    if (!keys.contains(key)) {
      keys.append(key);
    }
  }
  QStringList lines;
  for (const QString& key : keys) {
    lines.append(QStringLiteral("hl.unbind(%1)")
                     .arg(HyprlandGameModeCompositor::luaString(key)));
  }
  return lines.join(QLatin1Char('\n'));
}

QString GameModeShortcut::takenBy(const QByteArray& bindsJson, const Binding& binding) {
  const QJsonDocument document = QJsonDocument::fromJson(bindsJson);
  for (const QJsonValue& value : document.array()) {
    const QJsonObject bind = value.toObject();
    if (bind.value(QLatin1String("modmask")).toInt() != binding.modmask ||
        bind.value(QLatin1String("key")).toString().compare(binding.keyName, Qt::CaseInsensitive) != 0) {
      continue;
    }
    const QString description = bind.value(QLatin1String("description")).toString().trimmed();
    // Descriptions are user chosen; another launcher can also be called Game Mode.
    // Lua bindings expose only a callback ID, so ownership cannot be inferred from
    // them. Our own file binding is already recognized before this conflict check.
    const QString dispatcher = bind.value(QLatin1String("dispatcher")).toString();
    const QString argument = bind.value(QLatin1String("arg")).toString().trimmed();
    if (dispatcher == QLatin1String("exec") && binding.commands.contains(argument)) {
      continue;
    }
    return description.isEmpty() ? QStringLiteral("another shortcut") : description;
  }
  return {};
}

QString GameModeShortcut::displayKey(const QString& key) {
  QStringList parts;
  for (const QString& part : key.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
    const QString word = part.trimmed();
    parts.append(word.size() > 1 ? word.at(0).toUpper() + word.mid(1).toLower() : word.toUpper());
  }
  return parts.join(QStringLiteral(" + "));
}

GameModeShortcut::GameModeShortcut(const QString& bindingsPath, bool omarchy, QObject* parent, Binding binding)
    : QObject(parent), m_binding(std::move(binding)), m_path(bindingsPath), m_omarchy(omarchy) {
  connect(&m_watcher, &QFutureWatcher<Outcome>::finished, this, [this] {
    const Outcome outcome = m_watcher.result();
    m_available = outcome.available;
    m_boundKey = outcome.boundKey;
    m_takenBy = outcome.takenBy;
    m_statusText = outcome.statusText;
    emit changed();
  });
}

GameModeShortcut::~GameModeShortcut() { m_watcher.waitForFinished(); }

QString GameModeShortcut::keyLabel() const {
  return displayKey(m_boundKey.isEmpty() ? m_binding.key : m_boundKey);
}

void GameModeShortcut::refresh() { start(Action::Refresh); }

void GameModeShortcut::add() { start(Action::Add); }

void GameModeShortcut::remove() { start(Action::Remove); }

void GameModeShortcut::start(Action action) {
  if (m_path.isEmpty() || m_watcher.isRunning()) {
    return;
  }
  m_watcher.setFuture(QtConcurrent::run(
      [path = m_path, omarchy = m_omarchy, action, binding = m_binding] { return run(path, omarchy, action, binding); }));
  emit changed();
}

GameModeShortcut::Outcome GameModeShortcut::run(const QString& path, bool omarchy,
                                                Action action, const Binding& binding) {
  Outcome outcome;
  QString contents;
  QByteArray binds;
  if (!omarchy || !readFile(path, &contents) ||
      !hyprctl({QStringLiteral("-j"), QStringLiteral("binds")}, &binds)) {
    return outcome;
  }
  outcome.available = true;
  outcome.boundKey = boundKey(contents, binding);
  outcome.takenBy = outcome.boundKey.isEmpty() ? takenBy(binds, binding) : QString{};

  if (action == Action::Add && outcome.boundKey.isEmpty() && outcome.takenBy.isEmpty()) {
    if (!writeFile(path, withBinding(contents, binding))) {
      outcome.statusText = QStringLiteral("Could not write %1.").arg(path);
      return outcome;
    }
    outcome.boundKey = binding.key;
    // The file covers every later session. This makes the key work now, whether or not
    // the running Hyprland rereads its configuration on its own. When it does, the
    // reload and this can land in either order, so the key is cleared first to end up
    // with one binding rather than two that each run the command.
    hyprctl({QStringLiteral("eval"), liveBindingScript(binding)});
  } else if (action == Action::Remove && !outcome.boundKey.isEmpty()) {
    if (!writeFile(path, withoutBinding(contents, binding))) {
      outcome.statusText = QStringLiteral("Could not write %1.").arg(path);
      return outcome;
    }
    hyprctl({QStringLiteral("eval"), removalScript(contents, binding)});
    outcome.boundKey.clear();
    if (hyprctl({QStringLiteral("-j"), QStringLiteral("binds")}, &binds)) {
      outcome.takenBy = takenBy(binds, binding);
    }
  }
  return outcome;
}
