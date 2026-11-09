#pragma once

#include "tracking/AttributionAdapter.h"
#include "tracking/ProcFs.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <limits>

// Emulator launch profiles drive session attribution. A profile names the
// binaries an emulator runs under; the matcher then looks for a command line
// argument that looks like a game image. That covers every launch path that
// names the game on the command line: Omakade launches, terminal launches, and
// wrapper scripts. Loading a game from inside the emulator's own file picker
// shows no path on the command line, so two weaker kinds of evidence are
// consulted for those, in this order:
//
//  1. an attribution adapter, which reads a record the emulator rewrites while
//     the game runs (see tracking/AttributionAdapter.h). Session-verified.
//  2. the window title, matched against the titles the sources already know.
//     Weak: a title is decoration, and it is not evidence that the game is
//     loaded rather than merely named on screen.
//
// A process that names its own game on the command line is never asked about
// again, so weak evidence cannot overwrite strong evidence.
struct SessionProcessProfile {
  QString name;
  QStringList binaries;
  // Exact process/launcher paths for packages whose executable name is generic, such as
  // an AppRun wrapper. This is intentionally narrower than matching a basename globally.
  QStringList executablePaths;
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
  // The session may move to another process of the same game when this one exits, as when
  // a launcher's bootstrap ends and the game it started keeps running.
  bool handsOver = false;
};

struct ProcessProfileSet {
  QVector<SessionProcessProfile> emulators;
  QSet<QString> romExtensions;
};

namespace ProcessMatcher {

// Reads a profiles JSON document: {"romExtensions": [...],
// "emulators": [{"name": "...", "binaries": [...], "executablePaths": [...],
//                 "rescanSource": "..."}]}.
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

// The session-verified evidence an attribution adapter contributes for one process. The
// process start time is passed through so the adapter can keep its own state per process, and
// so a match built from it carries a verified process identity.
using AttributionResolver =
    std::function<AttributionAdapter::Result(qint64 pid, qint64 procStart, const QString& emulator)>;

// Three kinds of evidence in one pass, strongest first: the command line, then an attribution
// adapter, then the window title. A process is asked about a weaker kind only when no stronger
// kind matched it, so a title can never override a game the process named or the adapter
// proved. Without a resolver the corresponding step is skipped.
[[nodiscard]] QVector<SessionMatch> matchWithAttribution(
    const QVector<ProcessSnapshot>& processes, const ProcessProfileSet& profiles,
    const std::function<QString(qint64 pid)>& windowTitleForPid, const TitleResolver& resolveTitle,
    const AttributionResolver& resolveAttribution);
// Launchers such as Xodus Nexus run each game in a Wine prefix of its own, from a
// temporary copy of the game whose path says nothing about which game it is. The prefix
// does: `games` maps a canonical prefix to the game path to record against. Each prefix
// gives one match, its oldest Windows program outside C:\windows. The match hands over,
// so when that program exits and another in the prefix runs on, the session continues.
[[nodiscard]] QVector<SessionMatch> matchWinePrefixes(const QVector<ProcessSnapshot>& processes,
                                                      const QHash<QString, QString>& games,
                                                      const QString& source);

// Steam starts every game under its reaper, which lives as long as the game:
// `reaper SteamLaunch AppId=<id> -- <the game's command>`. Returns the app id from the
// reaper's own options, before the `--`, or an empty string for any other process and for
// the setup Steam runs before a first launch (`Install=1`).
[[nodiscard]] QString steamLaunchAppId(const QStringList& arguments);

// How long a reaper runs before it is matched. Steam starts the game through its launch
// wrapper and often the Steam Linux Runtime container first, so for a while the reaper's
// tree does not show the emulator it is about to run. Matching it then would open a short
// Steam session that ends as soon as the emulator appears.
inline constexpr qint64 kSteamSettleSeconds = 10;

// What the Steam matcher remembers between polls: the reapers (pid and procfs start time)
// that have been seen running an emulator. Such a reaper stays the emulator's play, so the
// seconds it spends cleaning up after the emulator exits are not recorded as Steam.
struct SteamLaunchState {
  QSet<QPair<qint64, qint64>> emulatorReapers;
};

// One match per running Steam game, recorded against its install folder: `installs` maps
// an app id to the folder the library knows the game by. A launch whose app id has no
// install folder, such as a non-Steam shortcut, is not recorded. When the reaper or any
// process it started runs an emulator from `profiles`, the emulator's own recording owns
// that play, so Steam does not record it a second time. The match is the reaper itself,
// the oldest one when Steam runs the same app id twice.
//
// A reaper that started after `settledBefore`, a procfs start time, is too new to tell
// whether it is about to run an emulator and is matched on a later poll. `state`, when
// given, carries the reapers seen running an emulator from one poll to the next.
[[nodiscard]] QVector<SessionMatch> matchSteamLaunches(
    const QVector<ProcessSnapshot>& processes, const QHash<QString, QString>& installs,
    const ProcessProfileSet& profiles,
    qint64 settledBefore = std::numeric_limits<qint64>::max(),
    SteamLaunchState* state = nullptr);
// The source name Steam sessions are recorded under, the same name the library gives Steam.
inline const QString kSteamSource = QStringLiteral("Steam");

// The profile file to read: a user copy under the config directory overrides the
// packaged one, which is the same order the recorder uses.
[[nodiscard]] QString profilesPath();

} // namespace ProcessMatcher
