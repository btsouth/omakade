#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// A native PC port of a console game: a recompilation or a source port that runs the game
// from the player's own dump.
struct NativePort {
  QString id;
  QString name;
  QString game;
  QString system;
  QString repo;
  bool linux = false;
  QStringList serials;
  QStringList titles;
  // Lowercase names its releases use for executables, AppImages and folders.
  QStringList files;
};

// The ports Omakade knows, from resources/native-ports.json. The monthly catalog workflow
// proposes additions; see docs/NATIVE-PORTS.md.
class PortCatalog final {
public:
  [[nodiscard]] static const PortCatalog& bundled();
  [[nodiscard]] static PortCatalog fromJson(const QByteArray& json, QString* error = nullptr);

  [[nodiscard]] const QVector<NativePort>& ports() const { return m_ports; }
  [[nodiscard]] const NativePort* find(const QString& id) const;
  // The port one of these file or folder names belongs to, or null.
  [[nodiscard]] const NativePort* matchFile(const QString& fileName) const;
  // Ports of a game, by its serial (CUSA01623, SLUS-21376) or title on a system.
  [[nodiscard]] QVector<const NativePort*> portsFor(const QString& system, const QString& serial,
                                                   const QString& title) const;

  [[nodiscard]] static QString normalizedSerial(const QString& serial);
  [[nodiscard]] static QString normalizedTitle(const QString& title);
  [[nodiscard]] static bool fileMatches(const QString& fileName, const QString& token);

private:
  QVector<NativePort> m_ports;
};
