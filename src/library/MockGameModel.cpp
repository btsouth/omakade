#include "library/MockGameModel.h"

#include <array>

namespace {
constexpr std::array<const char*, 25> kTitles = {
    "Aster Vale",    "Black Meridian", "Cinderwake",      "Cloud Harbor",  "Copper & Moss",
    "Dead Signal",   "Driftline",      "Echoes Below",    "Ember Circuit", "Far Meridian",
    "Glass Horizon", "Hollow Atlas",   "Iron Bloom",      "Last Light",    "Moonwake",
    "Neon Pilgrim",  "Northstar",      "Pale Frontier",   "Quiet Engine",  "Rook & Ruin",
    "Signal Lost",   "Sundown Relay",  "The Long Return", "Velvet Static", "Wild Orbit",
};

constexpr std::array<const char*, 10> kGenres = {
    "Adventure",  "Action RPG", "Strategy", "Exploration", "Roguelite",
    "Simulation", "Puzzle",     "Tactical", "Racing",      "Survival",
};

constexpr std::array<const char*, 8> kMarks = {
    "◇", "△", "○", "✦", "⌁", "□", "✧", "⬡",
};

constexpr std::array<const char*, 12> kColors = {
    "#509475", "#7aa2f7", "#d2689c", "#e0af68", "#449dab", "#9ece6a",
    "#eb927b", "#ad8ee6", "#2dd5b7", "#ff5345", "#7da6ff", "#75bbb3",
};

QString descriptionFor(int index, const QString& title) {
  static const std::array<const char*, 6> descriptions = {
      "Chart a quiet world of strange machines, forgotten paths, and choices that linger.",
      "Build your crew, read the terrain, and turn a desperate expedition into a way home.",
      "A precise, atmospheric journey where every shortcut hides a story worth finding.",
      "Push beyond the mapped frontier and piece together what happened before the signal died.",
      "Master a shifting system of tools, rivals, and environments that rewards curiosity.",
      "Move at your own pace through a handcrafted world filled with difficult, beautiful "
      "decisions.",
  };
  return QStringLiteral("%1. %2").arg(
      title, QString::fromUtf8(descriptions.at(index % descriptions.size())));
}
} // namespace

MockGameModel::MockGameModel(QObject* parent, int gameCount) : QAbstractListModel(parent) {
  m_games.reserve(gameCount);

  for (int index = 0; index < gameCount; ++index) {
    const int edition = index / static_cast<int>(kTitles.size());
    QString title = QString::fromUtf8(kTitles.at(index % kTitles.size()));
    if (edition > 0) {
      title += QStringLiteral(" %1").arg(edition + 1);
    }

    const int total = 12 + ((index * 7) % 43);
    const int unlocked = (index * 11) % (total + 1);

    m_games.push_back({
        .title = title,
        .subtitle = QString::fromUtf8(kGenres.at(index % kGenres.size())),
        .description = descriptionFor(index, title),
        .hours = (index * 17 + 3) % 241,
        .progress = total == 0 ? 0 : (unlocked * 100) / total,
        .achievementsUnlocked = unlocked,
        .achievementsTotal = total,
        .favorite = index % 9 == 0 || index == 3,
        .recent = index < 8 || index % 17 == 0,
        .lastPlayed = (index < 8 || index % 17 == 0) ? 1700000000 - index : 0,
        .accentStart = QColor(QString::fromUtf8(kColors.at(index % kColors.size()))),
        .accentEnd = QColor(QString::fromUtf8(kColors.at((index * 5 + 3) % kColors.size()))),
        .coverMark = QString::fromUtf8(kMarks.at(index % kMarks.size())),
        .year = 2014 + ((index * 3) % 13),
        .appId = QStringLiteral("demo-%1").arg(index),
        .coverPath = {},
        .heroPath = {},
        .logoPath = {},
        .installPath = {},
    });
  }
}

int MockGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant MockGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }

  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> MockGameModel::roleNames() const {
  return {
      {GameRoles::Title, "title"},
      {GameRoles::Subtitle, "subtitle"},
      {GameRoles::Description, "description"},
      {GameRoles::Hours, "hours"},
      {GameRoles::Progress, "progress"},
      {GameRoles::AchievementsUnlocked, "achievementsUnlocked"},
      {GameRoles::AchievementsTotal, "achievementsTotal"},
      {GameRoles::Favorite, "favorite"},
      {GameRoles::Recent, "recent"},
      {GameRoles::LastPlayed, "lastPlayed"},
      {GameRoles::AccentStart, "accentStart"},
      {GameRoles::AccentEnd, "accentEnd"},
      {GameRoles::CoverMark, "coverMark"},
      {GameRoles::Year, "year"},
      {GameRoles::AppId, "appId"},
      {GameRoles::CoverPath, "coverPath"},
      {GameRoles::HeroPath, "heroPath"},
      {GameRoles::LogoPath, "logoPath"},
      {GameRoles::InstallPath, "installPath"},
      {GameRoles::Source, "source"},
      {GameRoles::Runner, "runner"},
      {GameRoles::Flatpak, "flatpak"},
      {GameRoles::Hidden, "hidden"},
      {GameRoles::Installed, "installed"},
  };
}

QVariantMap MockGameModel::get(int row) const {
  if (row < 0 || row >= m_games.size()) {
    return {};
  }

  QVariantMap values;
  const auto roles = roleNames();
  for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator) {
    values.insert(QString::fromUtf8(iterator.value()),
                  valueForRole(m_games.at(row), iterator.key()));
  }
  return values;
}

void MockGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size()) {
    return;
  }

  m_games[row].favorite = !m_games.at(row).favorite;
  const QModelIndex changed = index(row);
  emit dataChanged(changed, changed, {GameRoles::Favorite});
}

QVariant MockGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.title;
  case GameRoles::Subtitle:
    return game.subtitle;
  case GameRoles::Description:
    return game.description;
  case GameRoles::Hours:
    return game.hours;
  case GameRoles::Progress:
    return game.progress;
  case GameRoles::AchievementsUnlocked:
    return game.achievementsUnlocked;
  case GameRoles::AchievementsTotal:
    return game.achievementsTotal;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return game.recent;
  case GameRoles::LastPlayed:
    return game.lastPlayed;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.coverMark;
  case GameRoles::Year:
    return game.year;
  case GameRoles::AppId:
    return game.appId;
  case GameRoles::CoverPath:
    return game.coverPath;
  case GameRoles::HeroPath:
    return game.heroPath;
  case GameRoles::LogoPath:
    return game.logoPath;
  case GameRoles::InstallPath:
    return game.installPath;
  case GameRoles::Source:
    return game.source;
  case GameRoles::Runner:
    return QString{};
  case GameRoles::Flatpak:
    return false;
  case GameRoles::Hidden:
    return false;
  case GameRoles::Installed:
    return true;
  default:
    return {};
  }
}
