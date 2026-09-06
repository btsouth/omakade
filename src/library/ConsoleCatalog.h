#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct ConsoleDefinition {
  QString id;
  QString displayName;
  QString libretroPlaylist;
  QStringList aliases;
  QStringList folderNames;
  QStringList extensions;
  QStringList standaloneExecutables;
  QStringList retroArchCores;
  bool dedicatedSource = false;
};

class ConsoleCatalog final {
public:
  [[nodiscard]] static const QVector<ConsoleDefinition>& all();
  [[nodiscard]] static const ConsoleDefinition* find(const QString& raw);
  [[nodiscard]] static QString idFor(const QString& raw);
  [[nodiscard]] static QString displayNameFor(const QString& raw);
  [[nodiscard]] static bool isDedicatedSource(const QString& raw);
  [[nodiscard]] static QString libretroPlaylistFor(const QString& raw);
  // Systems that sit behind a console card unless the user changes their layout.
  [[nodiscard]] static QStringList defaultCardSystems();
};
