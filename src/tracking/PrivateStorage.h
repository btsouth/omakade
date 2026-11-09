#pragma once

#include <QString>

// Keeps Omakade's own files readable only by the user who owns them.
//
// The library database, settings and caches hold game paths, play history and account
// names. Both the app and omakade-sessiond can create them, so each sets owner-only modes
// itself instead of relying on the umask. The umask is never changed: games inherit it.
namespace PrivateStorage {

// Creates path and any missing parents, and leaves the last directory owner-only (0700).
// Returns whether path is a directory afterwards.
bool makeDirectory(const QString& path);

// Sets an existing directory this user owns to 0700, or an existing file to 0600.
// Symbolic links and anything owned by someone else are left alone.
void restrictDirectory(const QString& path);
void restrictFile(const QString& path);

// Restricts a SQLite database and its -wal, -shm and -journal files to 0600.
void restrictDatabase(const QString& path);

// Creates the Omakade data, config, state and cache directories, and Qt's per-application
// Omakade folders, owner-only, and tightens directories and files an earlier version left
// readable by other users.
void secureDefaults();

} // namespace PrivateStorage
