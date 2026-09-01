#pragma once

#include <QObject>
#include <QString>

class AppSettings final : public QObject {
  Q_OBJECT
  Q_PROPERTY(
      bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
  Q_PROPERTY(int artworkCacheLimitMb READ artworkCacheLimitMb WRITE setArtworkCacheLimitMb NOTIFY
                 artworkCacheLimitMbChanged)
  Q_PROPERTY(QString steamId READ steamId WRITE setSteamId NOTIFY steamIdChanged)
  Q_PROPERTY(
      QString igdbClientId READ igdbClientId WRITE setIgdbClientId NOTIFY igdbClientIdChanged)
  Q_PROPERTY(bool steamEnabled READ steamEnabled WRITE setSteamEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool lutrisEnabled READ lutrisEnabled WRITE setLutrisEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool heroicEnabled READ heroicEnabled WRITE setHeroicEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool faugusEnabled READ faugusEnabled WRITE setFaugusEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(
      bool retroArchEnabled READ retroArchEnabled WRITE setRetroArchEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool closeAfterLaunch READ closeAfterLaunch WRITE setCloseAfterLaunch NOTIFY
                 closeAfterLaunchChanged)
  Q_PROPERTY(bool steamOwnedGames READ steamOwnedGames WRITE setSteamOwnedGames NOTIFY
                 steamOwnedGamesChanged)

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
  [[nodiscard]] bool steamEnabled() const;
  void setSteamEnabled(bool value);
  [[nodiscard]] bool lutrisEnabled() const;
  void setLutrisEnabled(bool value);
  [[nodiscard]] bool heroicEnabled() const;
  void setHeroicEnabled(bool value);
  [[nodiscard]] bool faugusEnabled() const;
  void setFaugusEnabled(bool value);
  [[nodiscard]] bool retroArchEnabled() const;
  void setRetroArchEnabled(bool value);
  [[nodiscard]] bool closeAfterLaunch() const;
  void setCloseAfterLaunch(bool value);
  [[nodiscard]] bool steamOwnedGames() const;
  void setSteamOwnedGames(bool value);

signals:
  void reducedMotionChanged();
  void artworkCacheLimitMbChanged();
  void steamIdChanged();
  void igdbClientIdChanged();
  void sourcesChanged();
  void closeAfterLaunchChanged();
  void steamOwnedGamesChanged();

private:
  [[nodiscard]] static QString defaultPath();
  void load();
  void save() const;

  QString m_path;
  bool m_reducedMotion = false;
  int m_artworkCacheLimitMb = 1024;
  QString m_steamId;
  QString m_igdbClientId;
  bool m_steamEnabled = true;
  bool m_lutrisEnabled = true;
  bool m_heroicEnabled = true;
  bool m_faugusEnabled = true;
  bool m_retroArchEnabled = true;
  bool m_closeAfterLaunch = false;
  bool m_steamOwnedGames = true;
};
