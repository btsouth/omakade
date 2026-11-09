#include "backup/BackupManager.h"
#include "app/AppSettings.h"
#include "backup/BackupSnapshot.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QtConcurrentRun>

namespace {
QString recordKey(const QString& table, const QJsonObject& row) {
  QJsonArray parts;
  if (table == "collections")
    parts.append(row.value("name").toString().toCaseFolded());
  else if (table == "manual_games" || table == "saved_filters")
    parts.append(row.value("id"));
  else if (table == "launch_preferences")
    parts.append(row.value("group_id"));
  else if (table == "automatic_links")
    parts.append(row.value("link_key"));
  else if (table == "game_metadata")
    parts.append(row.value("game_key"));
  else if (table == "play_sessions")
    parts.append(row.value("session_key"));
  else if (table == "play_baselines")
    parts.append(row.value("game_path"));
  else {
    if (table == "collection_games")
      parts.append(row.value("collection_name").toString().toCaseFolded());
    for (const auto& field : {"source", "runner", "app_id"})
      parts.append(row.value(field));
  }
  return QString::fromUtf8(QJsonDocument(parts).toJson(QJsonDocument::Compact));
}
// A shell or interpreter told to run code from its own arguments needs no file on
// disk, so a backup can carry the whole program. The preview flags these commands.
const QRegularExpression& interpreterName() {
  static const QRegularExpression pattern(
      QStringLiteral("^(sh|ash|bash|zsh|dash|ksh|mksh|csh|tcsh|fish|env|python[0-9.]*|"
                     "perl[0-9.]*|ruby[0-9.]*|node|nodejs|lua[0-9.]*|luajit)$"));
  return pattern;
}
// Wrappers and terminals start another program named in their arguments.
const QRegularExpression& wrapperName() {
  static const QRegularExpression pattern(
      QStringLiteral("^(gamemoderun|mangohud|prime-run|nice|ionice|chrt|taskset|setsid|nohup|"
                     "timeout|stdbuf|flatpak-spawn|steam-run|busybox|toybox|xterm|uxterm|"
                     "alacritty|kitty|foot|ghostty|wezterm|konsole|gnome-terminal|"
                     "xfce4-terminal|urxvt)$"));
  return pattern;
}
bool launchesProgram(const QString& word) {
  const QString name = QFileInfo(word).fileName();
  return interpreterName().match(name).hasMatch() || wrapperName().match(name).hasMatch();
}
// budget bounds the nested checks; running out counts as inline code.
bool runsInlineCode(const QString& executable, const QStringList& arguments, int* budget) {
  if (--*budget < 0)
    return true;
  const QString name = QFileInfo(executable).fileName();
  if (wrapperName().match(name).hasMatch()) {
    // Any later argument may be the program, which errs toward flagging.
    for (int i = 0; i < arguments.size(); ++i) {
      const QString& arg = arguments.at(i);
      if (launchesProgram(arg) && runsInlineCode(arg, arguments.mid(i + 1), budget))
        return true;
      // A terminal can take its whole command as one string, as in xterm -e "bash -c x".
      static const QRegularExpression space(QStringLiteral("\\s"));
      if (!arg.contains(space))
        continue;
      const QStringList words = QProcess::splitCommand(arg);
      if (!words.isEmpty() && launchesProgram(words.first()) &&
          runsInlineCode(words.first(), words.mid(1), budget))
        return true;
    }
    return false;
  }
  if (!interpreterName().match(name).hasMatch())
    return false;
  if (name == "env") {
    for (int i = 0; i < arguments.size(); ++i) {
      const QString& arg = arguments.at(i);
      if (arg == "-S" || arg.startsWith("--split-string") ||
          (arg.startsWith('-') && !arg.startsWith("--") && arg.contains('S')))
        return true;
      if (arg == "-u" || arg == "-C" || arg == "--unset" || arg == "--chdir")
        ++i;
      if (arg.startsWith('-') || arg.contains('='))
        continue;
      return runsInlineCode(arg, arguments.mid(i + 1), budget);
    }
    return false;
  }
  for (int i = 0; i < arguments.size(); ++i) {
    const QString& arg = arguments.at(i);
    if (arg.startsWith("--")) {
      for (const auto* option : {"--command", "--eval", "--print", "--init-command"})
        if (arg == option || arg.startsWith(QString::fromLatin1(option) + '='))
          return true;
      // node can load a module written into a data: URL.
      for (const auto* option : {"--import", "--loader", "--experimental-loader", "--require"}) {
        const QString prefix = QString::fromLatin1(option) + '=';
        if ((arg.startsWith(prefix) && arg.mid(prefix.size()).startsWith("data:")) ||
            (arg == option && i + 1 < arguments.size() &&
             arguments.at(i + 1).startsWith("data:")))
          return true;
      }
    } else if (arg.size() > 1 && arg.startsWith('-') &&
               (arg.contains('c') || arg.contains('e') || arg.contains('E') || arg == "-p")) {
      return true;
    }
  }
  return false;
}
struct LaunchCommand {
  QString executable;
  QStringList arguments;
  QString directory;
};
LaunchCommand launchCommand(const QJsonObject& row, QString* title = nullptr) {
  const auto entry = QJsonDocument::fromJson(row.value("entry").toString().toUtf8()).object();
  if (title)
    *title = entry.value("title").toString();
  QStringList arguments;
  for (const auto& value : entry.value("arguments").toArray())
    arguments.append(value.toString());
  return {entry.value("executable").toString(), arguments, entry.value("directory").toString()};
}
bool within(const QString& file, const QString& folder) {
  return file == folder || file.startsWith(folder + '/');
}
} // namespace

BackupManager::BackupManager(BackupRecovery::Paths paths, AppSettings* settings, bool available,
                             QObject* parent)
    : QObject(parent), m_paths(std::move(paths)), m_settings(settings),
      m_available(available && settings) {
  connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
    const auto result = m_watcher.result();
    m_busy = false;
    m_message = result.message;
    if (result.payload) {
      m_payload = result.payload;
      m_preview = result.preview;
    }
    if (result.queued) {
      m_payload.reset();
      m_preview.clear();
    }
    emit changed();
    if (result.queued)
      emit restoreQueued();
  });
}
BackupManager::~BackupManager() { m_watcher.waitForFinished(); }

bool BackupManager::begin(const QString& message) {
  if (m_busy)
    return false;
  if (!m_available) {
    m_message = "Backup and restore are available in your normal library.";
    emit changed();
    return false;
  }
  m_busy = true;
  m_message = message;
  emit changed();
  return true;
}
void BackupManager::run(std::function<Result()> operation) {
  m_watcher.setFuture(QtConcurrent::run(std::move(operation)));
}
QString BackupManager::localPath(const QString& input) {
  QString path = input;
  if (path.startsWith("file:")) {
    const QUrl url(path);
    if (!url.isLocalFile() || !url.host().isEmpty())
      return {};
    path = url.toLocalFile();
  }
  if (path.startsWith("~/"))
    path = QDir::homePath() + path.mid(1);
  if (!QDir::isAbsolutePath(path) || path.size() > 4096)
    return {};
  for (const auto c : path)
    if (c.category() == QChar::Other_Control)
      return {};
  return QDir::cleanPath(path);
}

void BackupManager::exportBackup(const QString& input) {
  if (!begin("Saving backup…"))
    return;
  const auto settings = m_settings->backupSettings();
  const auto paths = m_paths;
  const QString path = localPath(input);
  run([path, paths, settings] {
    Result result;
    const QString parent = QFileInfo(path).dir().canonicalPath();
    const QString resolved = parent + '/' + QFileInfo(path).fileName();
    const QString artwork = QFileInfo(paths.database).absolutePath() + "/artwork";
    if (path.isEmpty() || parent.isEmpty() || !path.endsWith(".omakade-backup") ||
        path == paths.database || path == paths.settings || within(resolved, paths.state) ||
        within(resolved, artwork) || QFileInfo(path).isSymLink()) {
      result.message =
          "Choose a local .omakade-backup file outside Omakade's artwork and recovery folders.";
      return result;
    }
    BackupPayload payload;
    QString error;
    if (!BackupSnapshot::capture(paths.database, settings, &payload, &error) ||
        !BackupArchive::write(path, payload, &error))
      result.message = error;
    else
      result.message = "Backup saved: " + path;
    return result;
  });
}

void BackupManager::previewBackup(const QString& input) {
  if (!begin("Reading backup…"))
    return;
  m_payload.reset();
  m_preview.clear();
  emit changed();
  const QString path = localPath(input);
  const auto paths = m_paths;
  const auto settings = m_settings->backupSettings();
  run([path, paths, settings] {
    Result result;
    if (path.isEmpty()) {
      result.message = "Choose a local backup file.";
      return result;
    }
    auto incoming = std::make_shared<BackupPayload>();
    BackupPayload current;
    QString error;
    if (!BackupArchive::read(path, incoming.get(), &error)) {
      result.message = error;
      return result;
    }
    bool damaged = false;
    if (!BackupSnapshot::capture(paths.database, settings, &current, &error)) {
      // Restore sets an unreadable library aside, so compare against nothing.
      damaged = BackupSnapshot::damaged(paths.database);
      if (!damaged) {
        result.message = error;
        return result;
      }
      current.settings = settings;
    }
    result.preview = describe(*incoming, current);
    result.preview.insert("path", path);
    result.preview.insert("currentDamaged", damaged);
    result.payload = std::move(incoming);
    result.message =
        damaged ? "Your library database is damaged and cannot be read. Restoring keeps an exact "
                  "copy of it in the recovery folder and rebuilds the library from this backup."
                : "Review the contents and choose how to restore them.";
    return result;
  });
}

void BackupManager::discardPreview() {
  if (m_busy)
    return;
  m_payload.reset();
  m_preview.clear();
  m_message.clear();
  emit changed();
}

void BackupManager::confirmRestore(bool replace) {
  if (!m_payload || !begin("Preparing restore…"))
    return;
  const auto incoming = m_payload;
  const auto paths = m_paths;
  run([incoming, paths, replace] {
    Result result;
    QString error;
    BackupRecovery recovery(paths);
    result.queued = recovery.stage(
        *incoming, replace ? BackupDatabase::Mode::Replace : BackupDatabase::Mode::Merge, &error);
    result.message = result.queued
                         ? "Restore is ready. Close Omakade and reopen it to apply the backup."
                         : error;
    return result;
  });
}

QVariantMap BackupManager::describe(const BackupPayload& incoming, const BackupPayload& current) {
  const QMap<QString, QString> names{{"game_metadata", "Game identifications"},
                                     {"play_sessions", "Recorded play sessions"},
                                     {"play_baselines", "Imported playtime baselines"},
                                     {"user_game_flags", "Favorites and hidden choices"},
                                     {"game_organization", "Completion states and tags"},
                                     {"collections", "Collections"},
                                     {"collection_games", "Collection memberships"},
                                     {"game_link_members", "Linked installations"},
                                     {"automatic_links", "Automatic link choices"},
                                     {"launch_preferences", "Preferred installations"},
                                     {"launch_activity", "Launch activity"},
                                     {"manual_games", "Manual games"},
                                     {"saved_filters", "Saved filters"},
                                     {"play_queue", "Up next"},
                                     {"artwork_overrides", "Custom artwork choices"}};
  QVariantList counts;
  for (auto it = names.begin(); it != names.end(); ++it) {
    const auto imported = incoming.library.value(it.key()).toArray();
    const auto existing = current.library.value(it.key()).toArray();
    QSet<QString> keys;
    for (const auto& row : existing)
      keys.insert(recordKey(it.key(), row.toObject()));
    int matching = 0;
    for (const auto& row : imported)
      if (keys.contains(recordKey(it.key(), row.toObject())))
        ++matching;
    counts.append(QVariantMap{{"label", it.value()},
                              {"incoming", imported.size()},
                              {"current", existing.size()},
                              {"matching", matching}});
  }
  QStringList missingPaths;
  int missingCount = 0;
  const auto noteMissing = [&](const QString& path) {
    ++missingCount;
    if (missingPaths.size() < 100)
      missingPaths.append(path);
  };
  for (const auto& value : incoming.library.value("manual_games").toArray()) {
    const auto entry =
        QJsonDocument::fromJson(value.toObject().value("entry").toString().toUtf8()).object();
    const QString executable = entry.value("executable").toString();
    if (!QFileInfo(executable).isFile())
      noteMissing(executable);
    const QString directory = entry.value("directory").toString();
    if (!directory.isEmpty() && !QFileInfo(directory).isDir())
      noteMissing(directory);
  }
  // Every manual entry that is new or runs differently becomes runnable from Play
  // after the restore, so the preview lists each command in full.
  QHash<QString, LaunchCommand> currentCommands;
  for (const auto& value : current.library.value("manual_games").toArray())
    currentCommands.insert(value.toObject().value("id").toString(),
                           launchCommand(value.toObject()));
  QVariantList launchCommands;
  int inlineCodeCount = 0;
  for (const auto& value : incoming.library.value("manual_games").toArray()) {
    const auto row = value.toObject();
    // Inactive rows are removed games; they restore but cannot be played.
    if (row.contains("active") && !row.value("active").toVariant().toBool())
      continue;
    QString title;
    const auto command = launchCommand(row, &title);
    const auto existing = currentCommands.constFind(row.value("id").toString());
    if (existing != currentCommands.constEnd() && existing->executable == command.executable &&
        existing->arguments == command.arguments && existing->directory == command.directory)
      continue;
    int budget = 64;
    const bool inlineCode = runsInlineCode(command.executable, command.arguments, &budget);
    inlineCodeCount += inlineCode;
    launchCommands.append(QVariantMap{{"title", title},
                                      {"executable", command.executable},
                                      {"arguments", command.arguments},
                                      {"directory", command.directory},
                                      {"isNew", existing == currentCommands.constEnd()},
                                      {"inlineCode", inlineCode}});
  }
  QString launchCommandWarning;
  if (!launchCommands.isEmpty()) {
    launchCommandWarning =
        QString::number(launchCommands.size()) +
        (launchCommands.size() == 1 ? " manual game launch command"
                                    : " manual game launch commands") +
        " from this backup, listed in the preview, will be able to run when you choose Play. "
        "Restore only backups you made or trust.";
    if (inlineCodeCount)
      launchCommandWarning +=
          " " + QString::number(inlineCodeCount) +
          (inlineCodeCount == 1 ? " of them runs code written into its arguments."
                                : " of them run code written into their arguments.");
  }
  for (const auto& value : incoming.settings.value("gog_library_paths").toArray()) {
    if (!QFileInfo(value.toString()).isDir())
      noteMissing(value.toString());
  }
  QVariantList settingChanges;
  for (auto setting = incoming.settings.begin(); setting != incoming.settings.end(); ++setting) {
    settingChanges.append(
        QVariantMap{{"name", setting.key()},
                    {"incoming", setting.value().toVariant()},
                    {"current", current.settings.value(setting.key()).toVariant()}});
  }
  QSet<QString> existingFilterNames;
  QSet<QString> incomingFilterIds;
  for (const auto& value : incoming.library.value("saved_filters").toArray())
    incomingFilterIds.insert(value.toObject().value("id").toString());
  for (const auto& value : current.library.value("saved_filters").toArray()) {
    const auto row = value.toObject();
    if (!incomingFilterIds.contains(row.value("id").toString()))
      existingFilterNames.insert(row.value("name_key").toString());
  }
  QStringList renamedFilters;
  for (const auto& value : incoming.library.value("saved_filters").toArray()) {
    const auto row = value.toObject();
    if (existingFilterNames.contains(row.value("name_key").toString()))
      renamedFilters.append(row.value("name").toString());
  }
  return {
      {"createdAt", incoming.createdAt},
      {"counts", counts},
      {"settingsCount", incoming.settings.size()},
      {"artworkCount", incoming.artwork.size()},
      {"missingPathCount", missingCount},
      {"missingPaths", missingPaths},
      {"settings", settingChanges},
      {"savedFilterNameConflicts", renamedFilters},
      {"launchCommands", launchCommands},
      {"launchCommandWarning", launchCommandWarning},
      {"mergeExplanation",
       "Merge keeps unrelated personal data. Imported values take precedence for matching games. "
       "Collections gain memberships; imported link groups take precedence for their members. "
       "Saved filters with a conflicting name receive a restored suffix. Play history is imported "
       "only for games with no local sessions or baseline; existing play history stays unchanged. "
       "Up next keeps its current order and appends new games, up to 100 entries."},
      {"replaceExplanation",
       "Replace clears current personal library choices, manual entries, and saved filters before "
       "importing the backup. Archived play history replaces current history when included; older "
       "backups without history leave it unchanged. Up next is replaced only when included in the "
       "backup. Game files stay in place."},
      {"recoveryExplanation",
       "Omakade saves a recovery copy before applying changes on the next startup. Account-service "
       "identifiers and Sunshine publishing choices remain local. Missing games stay stored for "
       "rediscovery, and missing manual paths can be repaired. Play-history restore requires the "
       "recorder to be stopped. Emulator saves and save states are not included. Restoring does "
       "not launch games."}};
}
