#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct LichiiGameRecord {
  QString gameId;  // "path:<file>": Lichii starts a game from its file
  QString titleId; // sixteen hex digits, uppercase, may be empty
  QString title;
  QString path; // the file handed to Lichii: .nup, .nsp, .xci, .nsz, .xcz or .nro
  QString coverPath;
  qint64 playtimeSeconds = 0;
  qint64 lastPlayed = 0;
};

// The three folders one Lichii setup keeps, following the XDG layout it inherits
// from yuzu: settings, user data (keys, play time) and cache (launch history).
struct LichiiInstall {
  QString configPath;
  QString dataPath;
  QString cachePath;
};

struct LichiiScanResult {
  QVector<LichiiGameRecord> games;
  QStringList roots; // Lichii configuration folders that were read
  QStringList warnings;
  bool incomplete = false;
};

class LichiiScanner final {
public:
  [[nodiscard]] static QVector<LichiiInstall> discoverInstalls();
  [[nodiscard]] static LichiiScanResult scan(const QVector<LichiiInstall>& installs);
  // Switch 2 titles use ids that start 04; Switch titles start 01.
  [[nodiscard]] static bool isSwitch2(const LichiiGameRecord& game);
};
