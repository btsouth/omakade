#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>

// The Steam games installed on this computer, as the Steam source keeps them in the
// library database. The recorder reads it to turn the app id Steam launched into the
// folder the library knows the game by, so a recorded Steam session belongs to its game.
namespace SteamInstalls {

// The install folder of a Steam game: steamapps/common/<install_dir> in its library. The
// Steam source reports the same string as the game's install path, which is what Stats and
// play history match recorded sessions against.
[[nodiscard]] QString installPath(const QString& libraryPath, const QString& installDirectory);

// App id to install folder for every installed Steam game. Empty when the table is missing
// or cannot be read, which leaves Steam games unrecorded rather than guessed at.
[[nodiscard]] QHash<QString, QString> read(const QSqlDatabase& database);

} // namespace SteamInstalls
