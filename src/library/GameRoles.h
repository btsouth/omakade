#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
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
  System,
  IsPortal,
  Pinned,
  MetadataKey,
  Rating,
  RatingCount,
  Popularity,
  // The artwork the game's own source provides, before any user choice or downloaded portrait.
  // Deciding whether a game needs a portrait has to look at this, not at the resolved cover,
  // or a portrait already downloaded would justify itself.
  SourceCoverPath,
  CustomHero,
  CustomLogo,
  PlaytimeSeconds,
  PlaytimeText,
  Genres,
  PlaytimeProvenance,
  NeedsIdentification,
};

inline QString formatPlaytime(qint64 seconds) {
  if (seconds <= 0)
    return QStringLiteral("0m");
  if (seconds < 60)
    return QStringLiteral("<1m");
  const qint64 minutes = seconds / 60;
  if (minutes < 60)
    return QString::number(minutes) + "m";
  const QString hours = QString::number(minutes / 60) + "h";
  return minutes % 60 ? hours + " " + QString::number(minutes % 60) + "m" : hours;
}

inline QHash<int, QByteArray> names() {
  return {
      {Title, "title"},
      {Genres, "genres"},
      {Subtitle, "subtitle"},
      {Description, "description"},
      {Hours, "hours"},
      {PlaytimeSeconds, "playtimeSeconds"},
      {PlaytimeText, "playtimeText"},
      {PlaytimeProvenance, "playtimeProvenance"},
      {Progress, "progress"},
      {AchievementsUnlocked, "achievementsUnlocked"},
      {AchievementsTotal, "achievementsTotal"},
      {Favorite, "favorite"},
      {Recent, "recent"},
      {LastPlayed, "lastPlayed"},
      {AccentStart, "accentStart"},
      {AccentEnd, "accentEnd"},
      {CoverMark, "coverMark"},
      {Year, "year"},
      {AppId, "appId"},
      {CoverPath, "coverPath"},
      {SourceCoverPath, "sourceCoverPath"},
      {HeroPath, "heroPath"},
      {LogoPath, "logoPath"},
      {InstallPath, "installPath"},
      {Source, "source"},
      {Runner, "runner"},
      {Flatpak, "flatpak"},
      {Hidden, "hidden"},
      {System, "system"},
      {IsPortal, "isPortal"},
      {Pinned, "pinned"},
      {MetadataKey, "metadataKey"},
      {NeedsIdentification, "needsIdentification"},
      {Rating, "rating"},
      {RatingCount, "ratingCount"},
      {Popularity, "popularity"},
  };
}
} // namespace GameRoles
