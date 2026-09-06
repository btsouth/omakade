#pragma once

#include <QRecursiveMutex>

// One lock for every keyring call in the process.
//
// Four services keep credentials: Steam, RetroAchievements, IGDB and SteamGridDB. Each reads
// its own key on a worker thread as the app starts, and libsecret registers its GObject types
// the first time a schema is built. Two threads doing that at the same moment race, and the
// registration fails with "cannot register existing type". Every schema built afterwards is
// invalid, so every lookup fails with "the attribute 'service' was not found in the password
// schema" and no credential loads at all. Nothing then depends on a key it cannot read, so
// identification simply never starts and the failure is silent.
//
// Hold this around any libsecret call. The work is a handful of lookups at startup, so
// serialising them costs nothing worth measuring. It is recursive because a worker takes it and
// then calls a helper that takes it again; with a plain mutex that thread waits on itself, the
// lookup never returns, and the service stays busy forever.
inline QRecursiveMutex& secretServiceLock() {
  static QRecursiveMutex lock;
  return lock;
}
