#pragma once

#include <Qt>

namespace GameRoles {
enum Role {
  Title = Qt::UserRole + 1,
  Subtitle,
  Description,
  Hours,
  Progress,
  AchievementsUnlocked,
  AchievementsTotal,
  Favorite,
  Recent,
  LastPlayed,
  AccentStart,
  AccentEnd,
  CoverMark,
  Year,
  AppId,
  CoverPath,
  HeroPath,
  LogoPath,
  InstallPath,
  Source,
  Runner,
  Flatpak,
  Hidden,
  CustomCover,
  Linked,
  LinkedSources,
  CompletionStatus,
  Tags,
  Collections,
  LaunchTarget,
  Installed,
};
}
