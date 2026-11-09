#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>

#include <optional>

// The Steam games installed on this computer, as the Steam source keeps them in the
// library database. The recorder reads it to turn the app id Steam launched into the
// folder the library knows the game by, so a recorded Steam session belongs to its game.
namespace SteamInstalls {

// The install folder of a Steam game: steamapps/common/<install_dir> in its library. The
// Steam source reports the same string as the game's install path, which is what Stats and
// play history match recorded sessions against.
[[nodiscard]] QString installPath(const QString& libraryPath, const QString& installDirectory);

// Whether the Steam source is on in Omakade's config.toml contents. Steam games are recorded
// only while it is, and an absent key means on, as in the app.
[[nodiscard]] bool sourceEnabled(const QString& configContents);

// App id to install folder for every installed Steam game. Nothing when the table is
// missing or cannot be read, so a caller can tell a failed read from no games installed.
[[nodiscard]] std::optional<QHash<QString, QString>> read(const QSqlDatabase& database);

// The name to show for a recorded session. A Steam session is recorded against its install
// folder, so its name is the game's title from the Steam source, or the whole folder name
// when the library no longer has it. Every other session is named from its game path.
[[nodiscard]] QString sessionTitle(const QSqlDatabase& database, const QString& source,
                                   const QString& gamePath);

} // namespace SteamInstalls
