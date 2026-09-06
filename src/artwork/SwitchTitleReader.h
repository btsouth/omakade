#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Reads the icon and display title that a Switch dump carries in its control
// data, using the console keys the user already installed for Ryujinx. Only the
// small control section is decrypted; nothing is written back to the dump and
// key material never leaves this process.
struct SwitchTitleInfo {
  QString titleId;   // 16 hex digits, uppercase, empty when unknown
  QString title;     // display name from the control data, empty when unknown
  QByteArray icon;   // JPEG bytes, empty when no icon was found
  QString failure;   // why nothing could be read, for diagnostics
  QStringList notes; // one line per content entry examined, for diagnostics
  [[nodiscard]] bool hasIcon() const { return !icon.isEmpty(); }
};

class SwitchTitleReader final {
public:
  // prod.keys candidates in the order Ryujinx and other tools use them.
  [[nodiscard]] static QStringList defaultKeyFiles();
  // title.keys candidates next to the prod.keys files above.
  [[nodiscard]] static QStringList defaultTitleKeyFiles();
  [[nodiscard]] static bool keysAvailable(const QStringList& keyFiles = defaultKeyFiles());

  [[nodiscard]] static SwitchTitleInfo read(const QString& romPath,
                                            const QStringList& keyFiles = defaultKeyFiles(),
                                            const QStringList& titleKeyFiles = defaultTitleKeyFiles());

  // Cached icon for a dump: extracts on first use into <cacheRoot>/<key>.jpg.
  // Returns the cache path, or an empty string when the dump has no icon.
  [[nodiscard]] static QString cachedIcon(const QString& romPath, const QString& cacheKey,
                                          const QString& cacheRoot, SwitchTitleInfo* info = nullptr);
  [[nodiscard]] static QString defaultCacheRoot();
};

// Primitives exposed for tests, which build synthetic dumps with the same crypto.
namespace SwitchCrypto {
QByteArray ecb(const QByteArray& key, const QByteArray& data, bool encrypt);
QByteArray ctr(const QByteArray& key, const QByteArray& iv, const QByteArray& data);
// XTS over 0x200-byte sectors with the sector number as a big-endian tweak.
QByteArray xtsSectors(const QByteArray& key, const QByteArray& data, quint64 firstSector,
                      bool encrypt);
}  // namespace SwitchCrypto
