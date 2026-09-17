#pragma once

#include <QString>

// Turns the game path stored on a recorded session into something a person can
// read. Emulators launch through entry points such as default.xex, eboot.bin, or
// boot.dol inside a named folder, so the bare file name would read as "default".
namespace SessionDisplay {

[[nodiscard]] QString titleForGamePath(const QString& gamePath);

} // namespace SessionDisplay
