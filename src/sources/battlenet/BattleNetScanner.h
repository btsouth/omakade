#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

struct BattleNetGameRecord {
  QString gameId;
  QString productId;
  QString title;
  QString launchCode;
  QString installPath;
  QString winePrefix;
  QString runner;
  QString coverPath;
  QString heroPath;
  qint64 lastPlayed = 0;
  bool flatpak = false;
};

struct BattleNetProductInstall {
  QString uid;
  QString productCode;
  QString installPath;
  bool installed = false;
  bool playable = false;
};

struct BattleNetScanResult {
  QVector<BattleNetGameRecord> games;
  QStringList prefixes;
  QStringList warnings;
  bool incomplete = false;
};

class BattleNetScanner final {
public:
  [[nodiscard]] static QStringList discoverPrefixes();
  [[nodiscard]] static BattleNetScanResult scan(const QStringList& prefixes);
  [[nodiscard]] static QString titleForProduct(const QString& productCode);
  [[nodiscard]] static QString launchCodeForProduct(const QString& productCode);
  [[nodiscard]] static QString productCodeFromId(const QString& id);
  [[nodiscard]] static QString gameIdFor(const QString& productCode, const QString& prefix);
  [[nodiscard]] static QString slugForProduct(const QString& productCode);
  [[nodiscard]] static QUrl coverUrl(const QString& productCode);
  [[nodiscard]] static QUrl heroUrl(const QString& productCode);
  [[nodiscard]] static bool isToolProduct(const QString& productCode);
  [[nodiscard]] static QByteArray encodeProductDb(const QVector<BattleNetProductInstall>& installs);
  [[nodiscard]] static QVector<BattleNetProductInstall> decodeProductDb(const QByteArray& data,
                                                                        bool* ok = nullptr);
};
