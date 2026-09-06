#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class AppSettings final : public QObject {
  Q_OBJECT
  Q_PROPERTY(
      bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
  Q_PROPERTY(int artworkCacheLimitMb READ artworkCacheLimitMb WRITE setArtworkCacheLimitMb NOTIFY
                 artworkCacheLimitMbChanged)
  Q_PROPERTY(QString steamId READ steamId WRITE setSteamId NOTIFY steamIdChanged)
  Q_PROPERTY(
      QString igdbClientId READ igdbClientId WRITE setIgdbClientId NOTIFY igdbClientIdChanged)
  Q_PROPERTY(QString retroAchievementsUsername READ retroAchievementsUsername WRITE
                 setRetroAchievementsUsername NOTIFY retroAchievementsUsernameChanged)
  Q_PROPERTY(bool steamEnabled READ steamEnabled WRITE setSteamEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool lutrisEnabled READ lutrisEnabled WRITE setLutrisEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool heroicEnabled READ heroicEnabled WRITE setHeroicEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool gogEnabled READ gogEnabled WRITE setGogEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool faugusEnabled READ faugusEnabled WRITE setFaugusEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(
      bool retroArchEnabled READ retroArchEnabled WRITE setRetroArchEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool pcsx2Enabled READ pcsx2Enabled WRITE setPcsx2Enabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool ryujinxEnabled READ ryujinxEnabled WRITE setRyujinxEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool shadps4Enabled READ shadps4Enabled WRITE setShadps4Enabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool cemuEnabled READ cemuEnabled WRITE setCemuEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool dolphinEnabled READ dolphinEnabled WRITE setDolphinEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(
      bool battleNetEnabled READ battleNetEnabled WRITE setBattleNetEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool consolePortalsEnabled READ consolePortalsEnabled WRITE setConsolePortalsEnabled
                 NOTIFY consolePortalsEnabledChanged)
  Q_PROPERTY(QStringList romFolders READ romFolders WRITE setRomFolders NOTIFY romFoldersChanged)
  Q_PROPERTY(QStringList consoleLayouts READ consoleLayouts NOTIFY consoleLayoutsChanged)
  Q_PROPERTY(QStringList cardSystems READ cardSystems NOTIFY consoleLayoutsChanged)
  Q_PROPERTY(QStringList fixedCardSystems READ fixedCardSystems NOTIFY consoleLayoutsChanged)
  Q_PROPERTY(QVariantList consoleSystems READ consoleSystems NOTIFY consoleLayoutsChanged)
  Q_PROPERTY(bool expandConsoles READ expandConsoles WRITE setExpandConsoles NOTIFY expandConsolesChanged)
  Q_PROPERTY(int consoleExpandLimit READ consoleExpandLimit WRITE setConsoleExpandLimit NOTIFY
                 consoleExpandLimitChanged)
  Q_PROPERTY(bool preferStandaloneEmulators READ preferStandaloneEmulators WRITE
                 setPreferStandaloneEmulators NOTIFY preferStandaloneEmulatorsChanged)
  Q_PROPERTY(bool closeAfterLaunch READ closeAfterLaunch WRITE setCloseAfterLaunch NOTIFY
                 closeAfterLaunchChanged)
  Q_PROPERTY(bool couchModeEnabled READ couchModeEnabled WRITE setCouchModeEnabled NOTIFY
                 couchModeEnabledChanged)
  Q_PROPERTY(QString couchLibraryView READ couchLibraryView WRITE setCouchLibraryView NOTIFY
                 couchLibraryViewChanged)
  Q_PROPERTY(int coverSize READ coverSize WRITE setCoverSize NOTIFY coverSizeChanged)
  Q_PROPERTY(int couchCoverSize READ couchCoverSize WRITE setCouchCoverSize NOTIFY couchCoverSizeChanged)
  Q_PROPERTY(int librarySortMode READ librarySortMode WRITE setLibrarySortMode NOTIFY
                 librarySortModeChanged)
  Q_PROPERTY(bool sunshineOmakadeApp READ sunshineOmakadeApp WRITE setSunshineOmakadeApp NOTIFY
                 sunshineChanged)
  Q_PROPERTY(bool sunshineGameApps READ sunshineGameApps WRITE setSunshineGameApps NOTIFY
                 sunshineChanged)

public:
  explicit AppSettings(const QString& path = {}, QObject* parent = nullptr);

  [[nodiscard]] bool reducedMotion() const;
  void setReducedMotion(bool value);
  [[nodiscard]] int artworkCacheLimitMb() const;
  void setArtworkCacheLimitMb(int value);
  [[nodiscard]] QString steamId() const;
  void setSteamId(const QString& value);
  [[nodiscard]] QString igdbClientId() const;
  void setIgdbClientId(const QString& value);
  [[nodiscard]] QString retroAchievementsUsername() const;
  void setRetroAchievementsUsername(const QString& value);
  [[nodiscard]] bool steamEnabled() const;
  void setSteamEnabled(bool value);
  [[nodiscard]] bool lutrisEnabled() const;
  void setLutrisEnabled(bool value);
  [[nodiscard]] bool heroicEnabled() const;
  void setHeroicEnabled(bool value);
  [[nodiscard]] bool gogEnabled() const;
  void setGogEnabled(bool value);
  [[nodiscard]] bool faugusEnabled() const;
  void setFaugusEnabled(bool value);
  [[nodiscard]] bool retroArchEnabled() const;
  void setRetroArchEnabled(bool value);
  [[nodiscard]] bool pcsx2Enabled() const;
  void setPcsx2Enabled(bool value);
  [[nodiscard]] bool ryujinxEnabled() const;
  void setRyujinxEnabled(bool value);
  [[nodiscard]] bool shadps4Enabled() const;
  void setShadps4Enabled(bool value);
  [[nodiscard]] bool cemuEnabled() const;
  void setCemuEnabled(bool value);
  // True while the user has not written an explicit pcsx2_enabled/ryujinx_enabled key,
  // letting the app enable the source automatically when its emulator is detected.
  [[nodiscard]] bool pcsx2AutoEnabled() const;
  [[nodiscard]] bool ryujinxAutoEnabled() const;
  [[nodiscard]] bool shadps4AutoEnabled() const;
  [[nodiscard]] bool dolphinEnabled() const;
  void setDolphinEnabled(bool value);
  [[nodiscard]] bool dolphinAutoEnabled() const;
  void setDolphinAutoEnabled(bool value);
  [[nodiscard]] bool cemuAutoEnabled() const;
  void setPcsx2AutoEnabled(bool value);
  void setRyujinxAutoEnabled(bool value);
  void setShadps4AutoEnabled(bool value);
  void setCemuAutoEnabled(bool value);
  [[nodiscard]] bool consolePortalsEnabled() const;
  void setConsolePortalsEnabled(bool value);
  [[nodiscard]] QStringList romFolders() const;
  void setRomFolders(const QStringList& value);
  Q_INVOKABLE void addRomFolder(const QString& path, const QString& system);
  Q_INVOKABLE void removeRomFolderAt(int index);
  // Explicit card/library overrides; absent entries follow the global view.
  [[nodiscard]] QStringList consoleLayouts() const;
  [[nodiscard]] QStringList cardSystems() const;
  [[nodiscard]] QStringList fixedCardSystems() const;
  [[nodiscard]] QVariantList consoleSystems() const;
  Q_INVOKABLE QString consoleLayout(const QString& system) const;
  Q_INVOKABLE void setConsoleLayout(const QString& system, const QString& layout);
  [[nodiscard]] bool expandConsoles() const;
  void setExpandConsoles(bool value);
  [[nodiscard]] int consoleExpandLimit() const;
  void setConsoleExpandLimit(int value);
  [[nodiscard]] bool preferStandaloneEmulators() const;
  void setPreferStandaloneEmulators(bool value);
  [[nodiscard]] bool battleNetEnabled() const;
  void setBattleNetEnabled(bool value);
  [[nodiscard]] bool closeAfterLaunch() const;
  void setCloseAfterLaunch(bool value);
  [[nodiscard]] bool couchModeEnabled() const;
  void setCouchModeEnabled(bool value);
  [[nodiscard]] QString couchLibraryView() const;
  void setCouchLibraryView(const QString& value);
  // Mirrors LibraryFilterModel::SortMode: 0 title, 1 recently played, 2 playtime, 3 rating, 4 popularity.
  int coverSize() const { return m_coverSize; }
  void setCoverSize(int value);
  int couchCoverSize() const { return m_couchCoverSize; }
  void setCouchCoverSize(int value);
  [[nodiscard]] int librarySortMode() const;
  void setLibrarySortMode(int value);
  [[nodiscard]] bool sunshineOmakadeApp() const;
  void setSunshineOmakadeApp(bool value);
  [[nodiscard]] bool sunshineGameApps() const;
  void setSunshineGameApps(bool value);

signals:
  void reducedMotionChanged();
  void artworkCacheLimitMbChanged();
  void steamIdChanged();
  void igdbClientIdChanged();
  void retroAchievementsUsernameChanged();
  void sourcesChanged();
  void closeAfterLaunchChanged();
  void couchModeEnabledChanged();
  void couchLibraryViewChanged();
  void librarySortModeChanged();
  void coverSizeChanged();
  void couchCoverSizeChanged();
  void sunshineChanged();
  void consolePortalsEnabledChanged();
  void romFoldersChanged();
  void consoleLayoutsChanged();
  void expandConsolesChanged();
  void consoleExpandLimitChanged();
  void preferStandaloneEmulatorsChanged();

private:
  [[nodiscard]] static QString defaultPath();
  void load();
  void save() const;

  QString m_path;
  bool m_reducedMotion = false;
  int m_artworkCacheLimitMb = 1024;
  QString m_steamId;
  QString m_igdbClientId;
  QString m_retroAchievementsUsername;
  bool m_steamEnabled = true;
  bool m_lutrisEnabled = true;
  bool m_heroicEnabled = true;
  bool m_gogEnabled = true;
  bool m_faugusEnabled = true;
  bool m_retroArchEnabled = true;
  bool m_pcsx2Enabled = false;
  bool m_ryujinxEnabled = false;
  bool m_shadps4Enabled = false;
  bool m_cemuEnabled = false;
  bool m_pcsx2Auto = true;
  bool m_ryujinxAuto = true;
  bool m_shadps4Auto = true;
  bool m_cemuAuto = true;
  bool m_dolphinEnabled = false;
  bool m_dolphinAuto = true;
  bool m_consolePortalsEnabled = true;
  QStringList m_romFolders;
  QStringList m_consoleLayouts;  // "system=card" or "system=library"
  bool m_expandConsoles = false;
  int m_consoleExpandLimit = 200;
  bool m_preferStandaloneEmulators = false;
  bool m_battleNetEnabled = true;
  bool m_closeAfterLaunch = false;
  bool m_couchModeEnabled = false;
  QString m_couchLibraryView = QStringLiteral("detail");
  int m_librarySortMode = 0;
  int m_coverSize = 100;
  int m_couchCoverSize = 100;
  bool m_sunshineOmakadeApp = false;
  bool m_sunshineGameApps = false;
};
