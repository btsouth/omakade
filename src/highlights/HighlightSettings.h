#pragma once

#include <QString>

// Highlight preferences in Omakade's config.toml. The app writes them with its other
// settings and the resident recorder in omakade-sessiond reads them on its own, so the
// parsing lives here once.
namespace HighlightSettings {

struct Values {
  // highlights = true: keep a replay buffer running while a game is in view.
  bool enabled = false;
  // highlight_seconds = 30: how much a clip keeps, one of kClipLengths.
  int clipSeconds = 30;
  // highlight_achievements = true: an achievement unlocked during play saves a clip.
  bool achievementClips = true;
};

inline constexpr int kClipLengths[] = {15, 30, 60, 120};

[[nodiscard]] Values parse(const QString& contents);
// Defaults when the file is missing or unreadable.
[[nodiscard]] Values load(const QString& configPath);
// The allowed length nearest to seconds.
[[nodiscard]] int normalizedSeconds(int seconds);
// Where clips are filed: the user's Videos folder, in Omakade/<game title>/.
[[nodiscard]] QString folder();
// How much the recorder keeps in memory. Longer than a clip, so an achievement clip can
// still reach back a full clip after the few seconds it waits for the moment to finish.
[[nodiscard]] int bufferSeconds(const Values& values);

} // namespace HighlightSettings
