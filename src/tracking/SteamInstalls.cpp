#include "tracking/SteamInstalls.h"

#include <QDir>
#include <QSqlQuery>

namespace SteamInstalls {

QString installPath(const QString& libraryPath, const QString& installDirectory) {
  return QDir(libraryPath + QStringLiteral("/steamapps/common")).absoluteFilePath(installDirectory);
}

QHash<QString, QString> read(const QSqlDatabase& database) {
  QHash<QString, QString> games;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral("SELECT app_id, library_path, install_dir FROM installations "
                                 "WHERE app_id <> '' AND library_path <> '' AND install_dir <> ''")))
    return games;
  while (query.next())
    games.insert(query.value(0).toString(),
                 installPath(query.value(1).toString(), query.value(2).toString()));
  return games;
}

} // namespace SteamInstalls
