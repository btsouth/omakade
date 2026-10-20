#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

// An application entry from a .desktop file, resolved the way a launcher runs it.
struct DesktopEntry {
  QString path;
  QString title;
  QString icon;
  QString executable;
  QStringList arguments;
  QString directory;
  QStringList categories;
  bool noDisplay = false;

  // Reads an application entry. GLib parses the escaping and argument quoting, so no shell text
  // runs. Entries that are hidden, need a terminal or use unsupported Exec field codes fail.
  [[nodiscard]] static std::optional<DesktopEntry> read(const QString& path,
                                                        QString* error = nullptr);
  // $XDG_DATA_HOME/applications, then each $XDG_DATA_DIRS/applications.
  [[nodiscard]] static QStringList applicationDirectories();
};

// A desktop entry with the paths its command refers to. When the command is a small script,
// such as a wrapper in ~/.local/bin, the paths named in the script count too.
struct DesktopLauncher {
  DesktopEntry entry;
  QStringList paths;
  // Programs the script runs from its working folder, as in exec ./game.x86_64.
  QStringList localPrograms;
  QString script;
};

class DesktopLaunchers final {
public:
  // Every readable application entry in the directories. The first entry of a file name wins,
  // as it does for a desktop session.
  [[nodiscard]] static QVector<DesktopLauncher> scan(const QStringList& directories);
  // Absolute paths named in a command or script, with $HOME and ~ expanded.
  [[nodiscard]] static QStringList pathsIn(const QString& text);
  [[nodiscard]] static QStringList localProgramsIn(const QString& text);
};
