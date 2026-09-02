#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

// RetroAchievements identifies a game by hashing its ROM file, and the exact rule (whole file vs.
// a fixed header stripped first) differs per console. This covers the subset of consoles whose
// rule is a plain or header-strip MD5 (see
// https://docs.retroachievements.org/developer-docs/game-identification.html); disc-based
// consoles that need custom parsing (PlayStation, N64, GameCube, ...) are out of scope.
enum class RetroAchievementsHashRule {
  WholeFileMd5,
  NesHeaderStrip,
  SnesHeaderStrip,
  PcEngineHeaderStrip,
  Atari7800HeaderStrip,
  AtariLynxHeaderStrip,
  Unsupported,
};

struct RetroAchievementsConsole {
  QString raConsoleName;
  RetroAchievementsHashRule rule = RetroAchievementsHashRule::Unsupported;
};

class RetroAchievementsHasher final {
public:
  // playlistDbName is RetroArch's playlist "db_name" (e.g. "Nintendo - Game Boy"). Matching is
  // normalized (case-insensitive, punctuation-insensitive) since the exact RetroArch database
  // name can vary slightly by libretro-database revision.
  [[nodiscard]] static RetroAchievementsConsole consoleFor(const QString& playlistDbName);
  [[nodiscard]] static std::optional<QByteArray> hashFile(const QString& contentPath,
                                                           RetroAchievementsHashRule rule);
};
