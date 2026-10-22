#pragma once

#include <QMetaType>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <functional>

class QObject;

// Saved highlight clips. The video files live in the user's Videos folder, one folder per
// game, where any player or file manager can reach them; the library database remembers
// which game each one belongs to and why it was saved. A clip whose file was moved or
// deleted outside Omakade is simply not listed.
//
// Clips are not part of Omakade backups: the table is not in the backup's table list, and
// the files are ordinary videos the user already owns.
namespace ClipLibrary {

struct Clip {
  qint64 id = 0;
  QString path;
  // The game as play sessions name it: a Steam app id or a content path, with its source.
  QString source;
  QString gameKey;
  // The game's title when the clip was saved, which also names its folder.
  QString title;
  qint64 createdAt = 0;
  int seconds = 0;
  // "button" for a clip the player asked for, "achievement" for an unlock.
  QString trigger;
  // What the clip shows, such as the achievement's name. Empty for a plain clip.
  QString label;
};

bool ensureSchema(QSqlDatabase& database);

// A folder name made from a game title: path separators and characters file managers and
// other systems refuse are replaced, and an empty or dot-only name becomes "Game".
[[nodiscard]] QString folderName(const QString& title);

// Moves the recorder's file into <root>/<folderName(title)>/ under the title and the time,
// and records it. The move is a rename, so the root has to be on the same filesystem as the
// recorder's directory, which keeps a half-copied clip from ever being listed. Returns the
// stored clip. When the video cannot be moved the clip has no path and error says why; when
// it was moved but the database refused the record, the clip keeps its path with id 0.
Clip file(QSqlDatabase& database, const QString& recordedPath, const QString& root, Clip clip,
          QString* error = nullptr);

// One game's clips, newest first, leaving out any whose file is gone.
[[nodiscard]] QVector<Clip> forGame(QSqlDatabase& database, const QString& source,
                                    const QString& gameKey);
[[nodiscard]] QVector<Clip> recent(QSqlDatabase& database, int limit);
[[nodiscard]] Clip byId(QSqlDatabase& database, qint64 id);
// Forgets the record. The caller decides what happens to the file.
bool forget(QSqlDatabase& database, qint64 id);

// Cuts `seconds` off the end of a video in place, without re-encoding. done(true) once the
// file is shorter; false leaves it as it was (no ffmpeg, nothing left to keep, or a failure).
void trimEnd(const QString& path, double seconds, QObject* context, std::function<void(bool)> done);

// Where the clip's still image is cached, keyed by its path and modification time.
[[nodiscard]] QString thumbnailPath(const QString& clipPath);
// Writes the still with ffmpeg, a third of the way in, unless it already exists. done gets
// the image path, or an empty string when there is no ffmpeg or it failed.
void makeThumbnail(const QString& clipPath, QObject* context, std::function<void(const QString&)> done);

} // namespace ClipLibrary
Q_DECLARE_METATYPE(ClipLibrary::Clip)
