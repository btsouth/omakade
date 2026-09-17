#pragma once

#include "tracking/ProcFs.h"

#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

// Emulator launch profiles drive session attribution. A profile names the
// binaries an emulator runs under; the matcher then looks for a command line
// argument that looks like a game image. That covers every launch path that
// names the game on the command line: Omakade launches, terminal launches, and
// wrapper scripts. Loading a game from inside the emulator's own file picker
// shows no path on the command line, so matchWithWindowTitles accepts a window
// title for those, matched against the titles the sources already know.
struct SessionProcessProfile {
  QString name;
  QStringList binaries;
  // Omakade source to ask for a rescan when a session of this emulator ends,
  // for emulators whose own playtime is only written on exit. Empty when the
  // source keeps itself current.
  QString rescanSource;
};

struct SessionMatch {
  qint64 pid = 0;
  qint64 procStart = -1;
  QString emulator;
  QString rescanSource;
  QString gamePath;
};

struct ProcessProfileSet {
  QVector<SessionProcessProfile> emulators;
  QSet<QString> romExtensions;
};

namespace ProcessMatcher {

// Reads a profiles JSON document: {"romExtensions": [...],
// "emulators": [{"name": "...", "binaries": [...], "rescanSource": "..."}]}.
// Returns an empty set and a non-empty error on malformed input.
[[nodiscard]] ProcessProfileSet load(const QString& path, QString* error = nullptr);

[[nodiscard]] QVector<SessionMatch> match(const QVector<ProcessSnapshot>& processes,
                                          const ProcessProfileSet& profiles);

// A window title that names a known game, used when the command line carries no
// game path because the game was loaded from the emulator's own file picker. The
// resolver returns the content path to record against, or an empty string when
// the title is unknown or ambiguous.
using TitleResolver = std::function<QString(const QString& windowTitle,
                                            const QString& emulator)>;

// True when a match only exists because of the window title, so callers can tell
// an attributed-from-title session from one that named its own game.
[[nodiscard]] bool matchCameFromWindowTitle(const SessionMatch& match);

// Same as match, but when an emulator process names no game on its command line,
// its window title is offered to the resolver. Without a resolver, or with an
// unresolved title, the process stays unmatched exactly as before.
[[nodiscard]] QVector<SessionMatch> matchWithWindowTitles(
    const QVector<ProcessSnapshot>& processes, const ProcessProfileSet& profiles,
    const std::function<QString(qint64 pid)>& windowTitleForPid, const TitleResolver& resolve);

} // namespace ProcessMatcher
