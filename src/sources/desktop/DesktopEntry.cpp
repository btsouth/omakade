#include "sources/desktop/DesktopEntry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

// GLib parses desktop-file escaping and argument quoting without executing shell text.
#include <glib.h>

namespace {
constexpr qint64 kMaximumEntryBytes = 1024 * 1024;
constexpr qint64 kMaximumScriptBytes = 256 * 1024;
constexpr int kMaximumPaths = 512;

QString stringValue(GKeyFile* file, const char* key) {
  gchar* raw = g_key_file_get_string(file, "Desktop Entry", key, nullptr);
  const QString value = QString::fromUtf8(raw ? raw : "");
  g_free(raw);
  return value;
}

std::optional<DesktopEntry> failed(QString* error, const QString& message) {
  if (error != nullptr) {
    *error = message;
  }
  return std::nullopt;
}

// A wrapper script's text, or nothing for a binary or a large file.
QString scriptText(const QString& path) {
  QFile file(path);
  if (file.size() > kMaximumScriptBytes || !file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray data = file.readAll();
  if (!data.startsWith("#!") || data.contains('\0')) {
    return {};
  }
  return QString::fromUtf8(data);
}
} // namespace

std::optional<DesktopEntry> DesktopEntry::read(const QString& path, QString* error) {
  if (QFileInfo(path).size() > kMaximumEntryBytes) {
    return failed(error, QStringLiteral("That desktop entry is too large"));
  }
  GKeyFile* file = g_key_file_new();
  const auto release = [&] { g_key_file_free(file); };
  if (!g_key_file_load_from_file(file, QFile::encodeName(path).constData(), G_KEY_FILE_NONE,
                                 nullptr)) {
    release();
    return failed(error, QStringLiteral("Could not read that desktop entry"));
  }
  if (stringValue(file, "Type") != QLatin1String("Application") ||
      stringValue(file, "Hidden") == QLatin1String("true") ||
      stringValue(file, "Terminal") == QLatin1String("true")) {
    release();
    return failed(error,
                  QStringLiteral("Choose an application entry that does not require a terminal"));
  }
  gchar* localized = g_key_file_get_locale_string(file, "Desktop Entry", "Name", nullptr, nullptr);
  DesktopEntry entry;
  entry.path = path;
  entry.title = QString::fromUtf8(localized ? localized : "");
  g_free(localized);
  entry.icon = stringValue(file, "Icon");
  entry.categories = stringValue(file, "Categories").split(QLatin1Char(';'), Qt::SkipEmptyParts);
  entry.noDisplay = stringValue(file, "NoDisplay") == QLatin1String("true");
  const QString command = stringValue(file, "Exec");
  const QString workdir = stringValue(file, "Path");
  const QString tryExec = stringValue(file, "TryExec");
  release();
  if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty()) {
    return failed(error,
                  QStringLiteral("The application required by this desktop entry is not installed"));
  }
  gint argc = 0;
  gchar** argv = nullptr;
  if (!g_shell_parse_argv(command.toUtf8().constData(), &argc, &argv, nullptr) || argc == 0) {
    return failed(error, QStringLiteral("The desktop entry has an invalid Exec command"));
  }
  QStringList args;
  bool valid = true;
  int fileCodes = 0;
  for (int n = 0; n < argc; ++n) {
    const QString token = QString::fromUtf8(argv[n]);
    if (n > 0 && token == QLatin1String("%i")) {
      if (!entry.icon.isEmpty())
        args.append({QStringLiteral("--icon"), entry.icon});
      continue;
    }
    QString expanded;
    bool omitted = false;
    for (int i = 0; i < token.size(); ++i) {
      if (token[i] != QLatin1Char('%')) {
        expanded += token[i];
        continue;
      }
      if (++i == token.size()) {
        valid = false;
        break;
      }
      const QChar code = token[i];
      if (code == QLatin1Char('%'))
        expanded += QLatin1Char('%');
      else if (n == 0)
        valid = false;
      else if (code == QLatin1Char('c'))
        expanded += entry.title;
      else if (code == QLatin1Char('k'))
        expanded += path;
      else if (QStringLiteral("fFuU").contains(code)) {
        ++fileCodes;
        if (token.size() != 2)
          valid = false;
        omitted = true;
      } else if (QStringLiteral("dDnNvm").contains(code))
        omitted = token.size() == 2;
      else
        valid = false;
    }
    if (!omitted)
      args.append(expanded);
  }
  g_strfreev(argv);
  if (!valid || fileCodes > 1 || args.isEmpty() || args.first().contains(QLatin1Char('='))) {
    return failed(error, QStringLiteral("The desktop entry uses unsupported Exec field codes"));
  }
  entry.executable = QStandardPaths::findExecutable(args.takeFirst());
  if (entry.executable.isEmpty()) {
    return failed(error, QStringLiteral("The desktop entry's executable could not be found"));
  }
  entry.arguments = args;
  entry.directory = workdir.isEmpty() ? QFileInfo(entry.executable).absolutePath() : workdir;
  return entry;
}

QStringList DesktopEntry::applicationDirectories() {
  QStringList directories;
  const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
  directories.append((dataHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share")
                                         : dataHome) +
                     QStringLiteral("/applications"));
  QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
  if (dataDirs.isEmpty()) {
    dataDirs = QStringLiteral("/usr/local/share:/usr/share");
  }
  for (const QString& directory : dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
    directories.append(QDir::cleanPath(directory) + QStringLiteral("/applications"));
  }
  directories.removeDuplicates();
  return directories;
}

QVector<DesktopLauncher> DesktopLaunchers::scan(const QStringList& directories) {
  QVector<DesktopLauncher> launchers;
  QSet<QString> seenNames;
  for (const QString& directory : directories) {
    const QFileInfoList files =
        QDir(directory).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
    for (const QFileInfo& file : files) {
      if (seenNames.contains(file.fileName())) {
        continue;
      }
      seenNames.insert(file.fileName());
      std::optional<DesktopEntry> entry = DesktopEntry::read(file.absoluteFilePath());
      if (!entry.has_value() || entry->noDisplay) {
        continue;
      }
      DesktopLauncher launcher{.entry = *entry, .paths = {}, .localPrograms = {},
                               .script = scriptText(entry->executable)};
      launcher.localPrograms = localProgramsIn(launcher.script);
      launcher.paths.append(entry->executable);
      launcher.paths += pathsIn(entry->arguments.join(QLatin1Char('\n')));
      for (const QString& argument : entry->arguments) {
        if (QDir::isAbsolutePath(argument)) {
          launcher.paths.append(QDir::cleanPath(argument));
        }
      }
      launcher.paths += pathsIn(launcher.script);
      launcher.paths.removeDuplicates();
      launchers.append(launcher);
    }
  }
  return launchers;
}

QStringList DesktopLaunchers::localProgramsIn(const QString& text) {
  QStringList programs;
  static const QRegularExpression local(QStringLiteral("(?:^|[\\s;&|(\"'])\\./([A-Za-z0-9._+-]+)"));
  for (auto it = local.globalMatch(text); it.hasNext() && programs.size() < kMaximumPaths;) {
    programs.append(it.next().captured(1));
  }
  programs.removeDuplicates();
  return programs;
}

QStringList DesktopLaunchers::pathsIn(const QString& text) {
  QStringList paths;
  if (text.isEmpty()) {
    return paths;
  }
  const QString home = QDir::homePath();
  const auto add = [&](QString candidate) {
    candidate = candidate.trimmed();
    for (const QString& prefix :
         {QStringLiteral("~/"), QStringLiteral("$HOME/"), QStringLiteral("${HOME}/")}) {
      if (candidate.startsWith(prefix)) {
        candidate = home + QLatin1Char('/') + candidate.mid(prefix.size());
        break;
      }
    }
    if (candidate.size() > 1 && candidate.startsWith(QLatin1Char('/')) &&
        candidate.size() <= 4096 && paths.size() < kMaximumPaths) {
      paths.append(QDir::cleanPath(candidate));
    }
  };
  static const QRegularExpression quoted(QStringLiteral("\"([^\"\\n]+)\"|'([^'\\n]+)'"));
  for (auto it = quoted.globalMatch(text); it.hasNext();) {
    const QRegularExpressionMatch match = it.next();
    add(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1));
  }
  // Unquoted words, as in kyty=/opt/kyty or exec ~/Games/port/run.
  static const QRegularExpression bare(
      QStringLiteral("(?:^|[\\s=:(])((?:~|\\$HOME|\\$\\{HOME\\})?/[^\\s\"'`;|&<>()]+)"));
  for (auto it = bare.globalMatch(text); it.hasNext();) {
    add(it.next().captured(1));
  }
  paths.removeDuplicates();
  return paths;
}
