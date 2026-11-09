#include "tracking/SteamInstalls.h"

#include "tracking/ProcessMatcher.h"
#include "tracking/SessionDisplay.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlQuery>

namespace SteamInstalls {

QString installPath(const QString& libraryPath, const QString& installDirectory) {
  return QDir(libraryPath + QStringLiteral("/steamapps/common")).absoluteFilePath(installDirectory);
}

bool sourceEnabled(const QString& configContents) {
  static const QRegularExpression pattern(
      QStringLiteral("(?m)^steam_enabled\\s*=\\s*(true|false)\\s*$"));
  const QRegularExpressionMatch match = pattern.match(configContents);
  return !match.hasMatch() || match.captured(1) == QStringLiteral("true");
}

std::optional<QHash<QString, QString>> read(const QSqlDatabase& database) {
  QHash<QString, QString> games;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral("SELECT app_id, library_path, install_dir FROM installations "
                                 "WHERE app_id <> '' AND library_path <> '' AND install_dir <> ''")))
    return std::nullopt;
  while (query.next())
    games.insert(query.value(0).toString(),
                 installPath(query.value(1).toString(), query.value(2).toString()));
  return games;
}

QString sessionTitle(const QSqlDatabase& database, const QString& source,
                     const QString& gamePath) {
  if (source != ProcessMatcher::kSteamSource || gamePath.trimmed().isEmpty())
    return SessionDisplay::titleForGamePath(gamePath);
  QSqlQuery query(database);
  if (query.exec(QStringLiteral("SELECT i.library_path, i.install_dir, g.title FROM installations i "
                                "JOIN games g ON g.app_id = i.app_id WHERE g.title <> ''"))) {
    while (query.next())
      if (installPath(query.value(0).toString(), query.value(1).toString()) == gamePath)
        return query.value(2).toString();
  }
  // A folder name such as "S.T.A.L.K.E.R. 2" has no extension to drop.
  const QString folder = QFileInfo(gamePath).fileName().simplified();
  return folder.isEmpty() ? SessionDisplay::titleForGamePath(gamePath) : folder;
}

} // namespace SteamInstalls
