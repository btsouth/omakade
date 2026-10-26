#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

// Xodus Nexus installs Microsoft Store and PC Game Pass games and keeps its library in
// launcher-state.json inside its profile directory.
struct XodusGameRecord {
  QString gameId;
  QString title;
  QString publisher;
  QString storeId;
  QString installPath;
  QString executable;
  // The Wine prefix Nexus runs the game in, when it exists yet.
  QString winePrefix;
  QString coverPath;
  QUrl heroUrl;
  bool antiCheat = false;
};

// Portrait and wide artwork the Microsoft Store catalog lists for a product. Nexus only
// keeps the square box art.
struct XodusStoreArt {
  QUrl poster;
  QUrl hero;
};

struct XodusScanResult {
  QVector<XodusGameRecord> games;
  QStringList roots;
  QStringList warnings;
  bool incomplete = false;
};

class XodusScanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  // Covers Nexus stores inline are written to artworkCache so the library can show
  // them offline. An empty artworkCache skips that.
  [[nodiscard]] static XodusScanResult scan(const QStringList& roots,
                                            const QString& artworkCache = {});
  [[nodiscard]] static bool validGameId(const QString& gameId);
  // The launch-game script Nexus writes into its profile, or empty when it is missing
  // or not safe to run.
  [[nodiscard]] static QString launchScript(const QString& givenRoot, bool game = true);
  // The xodus-cli Nexus installed and points at in its profile, or empty when it is missing
  // or not safe to run.
  [[nodiscard]] static QString backendCli(const QString& givenRoot);
  // The decimal Xbox title ID from the game's MicrosoftGame.config, or empty.
  [[nodiscard]] static QString xboxTitleId(const QString& installPath);
  [[nodiscard]] static QUrl safeArtworkUrl(const QString& value);
  [[nodiscard]] static bool validStoreId(const QString& storeId);
  // The public catalog request for up to 20 store IDs; invalid IDs are left out.
  [[nodiscard]] static QUrl catalogUrl(const QStringList& storeIds);
  // Poster and hero URLs by store ID, from a catalog response.
  [[nodiscard]] static QHash<QString, XodusStoreArt> parseCatalog(const QByteArray& json);
};
