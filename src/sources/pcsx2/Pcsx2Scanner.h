#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct Pcsx2GameRecord {
  QString gameId;      // serial (e.g. SLUS-20909) or path hash fallback
  QString title;
  QString path;        // absolute disc/ELF path
  QString serial;
  QString coverPath;
  QString region;
  qint64 playtimeSeconds = 0;
  qint64 lastPlayed = 0;
  bool isElf = false;  // true when PCSX2 classified the entry as an ELF
  bool flatpak = false;
};

struct Pcsx2ScanResult {
  QVector<Pcsx2GameRecord> games;
  QStringList roots;       // PCSX2 config roots that were scanned
  QStringList warnings;
  bool incomplete = false;
};

class Pcsx2Scanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static Pcsx2ScanResult scan(const QStringList& roots);
};