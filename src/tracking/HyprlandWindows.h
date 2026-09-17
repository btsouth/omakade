#pragma once

#include <QString>
#include <QVector>

// Window titles from the Hyprland compositor, used to attribute a session for a
// game that an emulator loaded from its own file picker. Those loads never put
// the game path on the command line, but the emulator usually puts the game's
// name in its window title.
//
// Every call is opt-in and best effort: without a compositor, without hyprctl,
// or with a stalled socket, the list comes back empty and nothing else changes.
namespace HyprlandWindows {

struct Window {
  QString title;
  qint64 pid = 0;
  QString address;
  // 0 means this is the compositor's focused window; higher numbers are further
  // back in focus history.
  int focusHistoryId = -1;
};

// Parses the `hyprctl clients -j` array. Returns an empty list and a non-empty
// error on malformed input, so a compositor answer is never half-read.
[[nodiscard]] QVector<Window> parse(const QByteArray& json, QString* error = nullptr);

// True when a compositor is present and hyprctl can be run.
[[nodiscard]] bool available();

// Lists the clients of the running compositor. Empty when unavailable.
[[nodiscard]] QVector<Window> list();

// The title of one process's window, matched by the compositor's pid. Empty when
// that process owns no window, which is the common case for a short-lived child.
[[nodiscard]] QString titleForPid(const QVector<Window>& windows, qint64 pid);

// True when this pid owns a window that is not the compositor's focused one. Used
// to stop billing time while a game sits unfocused behind other work. A pid with
// no window at all is not unfocused: it just is not on screen, and reporting it as
// unfocused would pause a run whose window has not appeared yet.
[[nodiscard]] bool isUnfocused(const QVector<Window>& windows, qint64 pid);

} // namespace HyprlandWindows
