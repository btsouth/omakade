#include "artwork/SwitchTitleReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>

#include <openssl/evp.h>

namespace {
constexpr qint64 kNcaHeaderBytes = 0xC00;
constexpr qint64 kSectorBytes = 0x200;
constexpr qint64 kMediaUnit = 0x200;
constexpr qint64 kMaximumControlSection = 64 * 1024 * 1024;
constexpr qint64 kMaximumIconBytes = 2 * 1024 * 1024;
constexpr int kNcaContentTypeControl = 2;

struct Keys {
  QByteArray headerKey;                       // 32 bytes
  QHash<int, QByteArray> keyAreaKeyApplication;  // by generation index
  QHash<int, QByteArray> titleKek;               // by generation index
  QHash<QString, QByteArray> titleKeys;          // rights id (lowercase hex) -> encrypted key
  [[nodiscard]] bool valid() const { return headerKey.size() == 32; }
};

QByteArray hexBytes(const QString& text) {
  static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]+$"));
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty() || trimmed.size() % 2 != 0 || !hex.match(trimmed).hasMatch()) {
    return {};
  }
  return QByteArray::fromHex(trimmed.toLatin1());
}

void loadKeyFile(const QString& path, Keys* keys) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 1024 * 1024) {
    return;
  }
  static const QRegularExpression generation(QStringLiteral("_([0-9a-fA-F]{2})$"));
  while (!file.atEnd()) {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const qsizetype separator = line.indexOf(QLatin1Char('='));
    if (line.startsWith(QLatin1Char('#')) || separator <= 0) {
      continue;
    }
    const QString name = line.left(separator).trimmed().toLower();
    const QByteArray value = hexBytes(line.mid(separator + 1));
    if (value.isEmpty()) {
      continue;
    }
    if (name == QLatin1String("header_key") && value.size() == 32) {
      keys->headerKey = value;
      continue;
    }
    const QRegularExpressionMatch match = generation.match(name);
    if (!match.hasMatch() || value.size() != 16) {
      continue;
    }
    const int index = match.captured(1).toInt(nullptr, 16);
    if (name.startsWith(QLatin1String("key_area_key_application_"))) {
      keys->keyAreaKeyApplication.insert(index, value);
    } else if (name.startsWith(QLatin1String("titlekek_"))) {
      keys->titleKek.insert(index, value);
    }
  }
}

void loadTitleKeyFile(const QString& path, Keys* keys) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 8 * 1024 * 1024) {
    return;
  }
  while (!file.atEnd()) {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const qsizetype separator = line.indexOf(QLatin1Char('='));
    if (separator <= 0) {
      continue;
    }
    const QByteArray rightsId = hexBytes(line.left(separator));
    const QByteArray key = hexBytes(line.mid(separator + 1));
    if (rightsId.size() == 16 && key.size() == 16) {
      keys->titleKeys.insert(QString::fromLatin1(rightsId.toHex()), key);
    }
  }
}

Keys loadKeys(const QStringList& keyFiles, const QStringList& titleKeyFiles) {
  Keys keys;
  for (const QString& path : keyFiles) {
    if (!keys.valid()) {
      loadKeyFile(path, &keys);
    }
  }
  for (const QString& path : titleKeyFiles) {
    loadTitleKeyFile(path, &keys);
  }
  return keys;
}

// A named region of the dump: an NCA inside an NSP (PFS0) or XCI (HFS0).
struct Entry {
  QString name;
  qint64 offset = 0;
  qint64 size = 0;
};

QByteArray readAt(QFile& file, qint64 offset, qint64 size) {
  if (offset < 0 || size <= 0 || offset + size > file.size() || !file.seek(offset)) {
    return {};
  }
  const QByteArray bytes = file.read(size);
  return bytes.size() == size ? bytes : QByteArray{};
}

// PFS0 (NSP) and HFS0 (XCI partition) share a layout; only the entry size differs.
QVector<Entry> readPartition(QFile& file, qint64 base, const char* magic, int entrySize) {
  QVector<Entry> entries;
  const QByteArray header = readAt(file, base, 0x10);
  if (header.size() != 0x10 || !header.startsWith(magic)) {
    return entries;
  }
  const auto count = qFromLittleEndian<quint32>(header.constData() + 4);
  const auto stringTableSize = qFromLittleEndian<quint32>(header.constData() + 8);
  if (count == 0 || count > 4096 || stringTableSize > 4 * 1024 * 1024) {
    return entries;
  }
  const qint64 tableBytes = static_cast<qint64>(count) * entrySize;
  const QByteArray table = readAt(file, base + 0x10, tableBytes);
  const QByteArray strings = readAt(file, base + 0x10 + tableBytes, stringTableSize);
  if (table.size() != tableBytes || strings.size() != static_cast<qsizetype>(stringTableSize)) {
    return entries;
  }
  const qint64 dataBase = base + 0x10 + tableBytes + stringTableSize;
  for (quint32 index = 0; index < count; ++index) {
    const char* record = table.constData() + static_cast<qsizetype>(index) * entrySize;
    Entry entry;
    entry.offset = dataBase + static_cast<qint64>(qFromLittleEndian<quint64>(record));
    entry.size = static_cast<qint64>(qFromLittleEndian<quint64>(record + 8));
    const auto nameOffset = qFromLittleEndian<quint32>(record + 16);
    if (nameOffset >= stringTableSize) {
      continue;
    }
    const int end = strings.indexOf('\0', nameOffset);
    entry.name = QString::fromUtf8(strings.constData() + nameOffset,
                                   (end < 0 ? strings.size() : end) - static_cast<int>(nameOffset));
    // Trimmed XCIs cut the file short inside the last partition; keep entries
    // that start inside the file and let reads past the end fail on their own.
    if (entry.size > 0 && entry.offset < file.size()) {
      entries.append(entry);
    }
  }
  return entries;
}

QVector<Entry> ncaEntries(QFile& file, QVector<Entry>* tickets, QStringList* notes) {
  const QByteArray head = readAt(file, 0, 0x200);
  if (head.size() != 0x200) {
    return {};
  }
  QVector<Entry> all;
  if (head.startsWith("PFS0")) {
    all = readPartition(file, 0, "PFS0", 0x18);
  } else if (head.mid(0x100, 4) == "HEAD") {
    // XCI: the root HFS0 lists partitions; game content lives in "secure".
    const auto rootOffset = static_cast<qint64>(qFromLittleEndian<quint64>(head.constData() + 0x130));
    for (const Entry& partition : readPartition(file, rootOffset, "HFS0", 0x40)) {
      if (notes != nullptr) {
        notes->append(QStringLiteral("xci partition %1 at %2 (%3 bytes)")
                          .arg(partition.name).arg(partition.offset).arg(partition.size));
      }
      if (partition.name == QLatin1String("secure") || partition.name == QLatin1String("normal")) {
        all += readPartition(file, partition.offset, "HFS0", 0x40);
      }
    }
  }
  QVector<Entry> ncas;
  for (const Entry& entry : all) {
    if (entry.name.endsWith(QLatin1String(".nca"), Qt::CaseInsensitive)) {
      ncas.append(entry);
    } else if (tickets != nullptr && entry.name.endsWith(QLatin1String(".tik"), Qt::CaseInsensitive)) {
      tickets->append(entry);
    }
  }
  return ncas;
}

struct ControlSection {
  qint64 ncaOffset = 0;      // absolute offset of the NCA in the dump
  qint64 sectionStart = 0;   // absolute offset of section 0
  qint64 sectionEnd = 0;
  qint64 romfsOffset = 0;    // relative to section start
  QByteArray ctrKey;
  QByteArray sectionCtr;     // 8 bytes from the FS header
};

bool zeroed(const QByteArray& bytes) {
  for (const char byte : bytes) {
    if (byte != 0) {
      return false;
    }
  }
  return true;
}

QByteArray ctrForOffset(const QByteArray& sectionCtr, qint64 absoluteOffset) {
  QByteArray iv(16, '\0');
  for (int index = 0; index < 8; ++index) {
    iv[index] = sectionCtr.at(7 - index);
  }
  const quint64 counter = static_cast<quint64>(absoluteOffset) >> 4;
  qToBigEndian<quint64>(counter, iv.data() + 8);
  return iv;
}

// Decrypts the header of every NCA until the control one turns up.
bool findControl(QFile& file, const Keys& keys, const QVector<Entry>& ncas,
                 const QVector<Entry>& tickets, ControlSection* control, QString* failure,
                 QStringList* notes) {
  for (const Entry& nca : ncas) {
    const QByteArray encrypted = readAt(file, nca.offset, kNcaHeaderBytes);
    if (encrypted.size() != kNcaHeaderBytes) {
      notes->append(QStringLiteral("%1: header unreadable").arg(nca.name));
      continue;
    }
    const QByteArray header = SwitchCrypto::xtsSectors(keys.headerKey, encrypted, 0, false);
    const int contentType = static_cast<unsigned char>(header.at(0x205));
    notes->append(QStringLiteral("%1: magic=%2 type=%3 size=%4")
                      .arg(nca.name, QString::fromLatin1(header.mid(0x200, 4)))
                      .arg(contentType)
                      .arg(nca.size));
    if (header.mid(0x200, 4) != "NCA3") {
      // NCA2 and older dumps are rare; NCA0 is pre-release. Skip rather than guess.
      continue;
    }
    if (contentType != kNcaContentTypeControl) {
      continue;
    }
    const int generationOld = static_cast<unsigned char>(header.at(0x206));
    const int generationNew = static_cast<unsigned char>(header.at(0x220));
    const int generation = qMax(generationOld, generationNew);
    const int keyIndex = generation == 0 ? 0 : generation - 1;
    const QByteArray rightsId = header.mid(0x230, 16);
    QByteArray ctrKey;
    if (zeroed(rightsId)) {
      const QByteArray kaek = keys.keyAreaKeyApplication.value(keyIndex);
      if (kaek.size() != 16) {
        *failure = QStringLiteral("prod.keys has no key_area_key_application_%1")
                       .arg(keyIndex, 2, 16, QLatin1Char('0'));
        return false;
      }
      const QByteArray keyArea = SwitchCrypto::ecb(kaek, header.mid(0x300, 0x40), false);
      ctrKey = keyArea.mid(0x20, 16);
    } else {
      const QByteArray kek = keys.titleKek.value(keyIndex);
      if (kek.size() != 16) {
        *failure = QStringLiteral("prod.keys has no titlekek_%1").arg(keyIndex, 2, 16, QLatin1Char('0'));
        return false;
      }
      QByteArray encryptedTitleKey;
      for (const Entry& ticket : tickets) {
        const QByteArray body = readAt(file, ticket.offset, qMin<qint64>(ticket.size, 0x2C0));
        if (body.size() >= 0x2B0 && body.mid(0x2A0, 16) == rightsId) {
          encryptedTitleKey = body.mid(0x180, 16);
          break;
        }
      }
      if (encryptedTitleKey.isEmpty()) {
        encryptedTitleKey = keys.titleKeys.value(QString::fromLatin1(rightsId.toHex()));
      }
      if (encryptedTitleKey.size() != 16) {
        *failure = QStringLiteral("no ticket or title key for this dump");
        return false;
      }
      ctrKey = SwitchCrypto::ecb(kek, encryptedTitleKey, false);
    }
    const auto mediaStart = qFromLittleEndian<quint32>(header.constData() + 0x240);
    const auto mediaEnd = qFromLittleEndian<quint32>(header.constData() + 0x244);
    const char* fsHeader = header.constData() + 0x400;
    const int fsType = static_cast<unsigned char>(fsHeader[2]);
    const int encryptionType = static_cast<unsigned char>(fsHeader[4]);
    if (fsType != 0 || encryptionType != 3 || QByteArray(fsHeader + 0x8, 4) != "IVFC") {
      *failure = QStringLiteral("control data is not a CTR-encrypted RomFS (fs=%1 enc=%2 hash=%3)")
                     .arg(fsType).arg(encryptionType).arg(QString::fromLatin1(QByteArray(fsHeader + 0x8, 4).toHex()));
      return false;
    }
    control->ncaOffset = nca.offset;
    control->sectionStart = nca.offset + static_cast<qint64>(mediaStart) * kMediaUnit;
    control->sectionEnd = nca.offset + static_cast<qint64>(mediaEnd) * kMediaUnit;
    control->romfsOffset = static_cast<qint64>(qFromLittleEndian<quint64>(fsHeader + 0x90));
    control->ctrKey = ctrKey;
    control->sectionCtr = QByteArray(fsHeader + 0x140, 8);
    if (control->sectionEnd - control->sectionStart > kMaximumControlSection ||
        control->sectionStart >= file.size()) {
      *failure = QStringLiteral("control section has an unexpected size");
      return false;
    }
    return true;
  }
  *failure = QStringLiteral("no control data found in the dump");
  return false;
}

// Reads and decrypts [offset, offset+size) of the control section, where
// offset is relative to the section start.
QByteArray readSection(QFile& file, const ControlSection& control, qint64 offset, qint64 size) {
  // Counter blocks are aligned to offsets inside the NCA, which itself may sit
  // at any offset in the container.
  const qint64 relative = control.sectionStart - control.ncaOffset + offset;
  const qint64 alignedRelative = relative & ~static_cast<qint64>(0xF);
  const qint64 skew = relative - alignedRelative;
  const qint64 alignedSize = ((size + skew + 0xF) & ~static_cast<qint64>(0xF));
  if (control.sectionStart + offset + size > control.sectionEnd) {
    return {};
  }
  const QByteArray encrypted = readAt(file, control.ncaOffset + alignedRelative, alignedSize);
  if (encrypted.isEmpty()) {
    return {};
  }
  const QByteArray plain = SwitchCrypto::ctr(control.ctrKey, ctrForOffset(control.sectionCtr, alignedRelative), encrypted);
  return plain.mid(skew, size);
}

QString nacpTitle(const QByteArray& nacp) {
  // 16 language entries of 0x300 bytes: 0x200 name, 0x100 publisher.
  for (int language = 0; language < 16; ++language) {
    const QByteArray raw = nacp.mid(language * 0x300, 0x200);
    const int end = raw.indexOf('\0');
    const QString name = QString::fromUtf8(end < 0 ? raw : raw.left(end)).trimmed();
    if (!name.isEmpty()) {
      return name;
    }
  }
  return {};
}

QString nacpTitleId(const QByteArray& nacp) {
  if (nacp.size() < 0x3040) {
    return {};
  }
  const auto id = qFromLittleEndian<quint64>(nacp.constData() + 0x3038);
  return id == 0 ? QString{} : QStringLiteral("%1").arg(id, 16, 16, QLatin1Char('0')).toUpper();
}

bool extract(QFile& file, const ControlSection& control, SwitchTitleInfo* info) {
  const QByteArray romfsHeader = readSection(file, control, control.romfsOffset, 0x50);
  if (romfsHeader.size() != 0x50 || qFromLittleEndian<quint64>(romfsHeader.constData()) != 0x50) {
    info->failure = QStringLiteral("control data did not decrypt to a RomFS");
    return false;
  }
  const auto fileTableOffset = static_cast<qint64>(qFromLittleEndian<quint64>(romfsHeader.constData() + 0x38));
  const auto fileTableSize = static_cast<qint64>(qFromLittleEndian<quint64>(romfsHeader.constData() + 0x40));
  const auto dataOffset = static_cast<qint64>(qFromLittleEndian<quint64>(romfsHeader.constData() + 0x48));
  if (fileTableSize <= 0 || fileTableSize > 1024 * 1024) {
    info->failure = QStringLiteral("control RomFS file table is invalid");
    return false;
  }
  const QByteArray fileTable = readSection(file, control, control.romfsOffset + fileTableOffset, fileTableSize);
  if (fileTable.size() != fileTableSize) {
    info->failure = QStringLiteral("could not read the control RomFS file table");
    return false;
  }
  struct RomFile {
    QString name;
    qint64 offset;
    qint64 size;
  };
  QVector<RomFile> files;
  qint64 cursor = 0;
  while (cursor + 0x20 <= fileTable.size()) {
    const char* entry = fileTable.constData() + cursor;
    const auto offset = static_cast<qint64>(qFromLittleEndian<quint64>(entry + 0x8));
    const auto size = static_cast<qint64>(qFromLittleEndian<quint64>(entry + 0x10));
    const auto nameLength = qFromLittleEndian<quint32>(entry + 0x1C);
    if (nameLength > 0x300 || cursor + 0x20 + nameLength > fileTable.size()) {
      break;
    }
    files.append({QString::fromUtf8(entry + 0x20, static_cast<int>(nameLength)), offset, size});
    cursor += 0x20 + ((nameLength + 3) & ~3);
  }
  const auto readRomFile = [&](const RomFile& romFile) {
    return readSection(file, control, control.romfsOffset + dataOffset + romFile.offset, romFile.size);
  };
  QString iconName;
  for (const RomFile& romFile : files) {
    if (romFile.name == QLatin1String("control.nacp") && romFile.size >= 0x4000 && romFile.size <= 0x8000) {
      const QByteArray nacp = readRomFile(romFile);
      info->title = nacpTitle(nacp);
      if (info->titleId.isEmpty()) {
        info->titleId = nacpTitleId(nacp);
      }
    } else if (romFile.name.startsWith(QLatin1String("icon_")) &&
               romFile.name.endsWith(QLatin1String(".dat")) && romFile.size > 0 &&
               romFile.size <= kMaximumIconBytes) {
      const bool preferred = romFile.name == QLatin1String("icon_AmericanEnglish.dat");
      if (info->icon.isEmpty() || preferred) {
        const QByteArray icon = readRomFile(romFile);
        if (icon.startsWith("\xFF\xD8\xFF")) {
          info->icon = icon;
          iconName = romFile.name;
        }
      }
    }
  }
  if (info->icon.isEmpty()) {
    info->failure = QStringLiteral("the control data has no icon");
  }
  return info->hasIcon() || !info->title.isEmpty();
}
}  // namespace

namespace SwitchCrypto {
QByteArray run(const EVP_CIPHER* cipher, const QByteArray& key, const QByteArray& iv,
               const QByteArray& data, bool encrypt) {
  EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
  QByteArray out(data.size() + 32, '\0');
  int written = 0;
  int total = 0;
  bool ok = context != nullptr &&
            EVP_CipherInit_ex(context, cipher, nullptr,
                              reinterpret_cast<const unsigned char*>(key.constData()),
                              iv.isEmpty() ? nullptr : reinterpret_cast<const unsigned char*>(iv.constData()),
                              encrypt ? 1 : 0) == 1;
  if (ok) {
    EVP_CIPHER_CTX_set_padding(context, 0);
    ok = EVP_CipherUpdate(context, reinterpret_cast<unsigned char*>(out.data()), &written,
                          reinterpret_cast<const unsigned char*>(data.constData()),
                          static_cast<int>(data.size())) == 1;
    total = written;
  }
  if (ok) {
    ok = EVP_CipherFinal_ex(context, reinterpret_cast<unsigned char*>(out.data()) + total, &written) == 1;
    total += written;
  }
  EVP_CIPHER_CTX_free(context);
  if (!ok) {
    return {};
  }
  out.resize(total);
  return out;
}

QByteArray ecb(const QByteArray& key, const QByteArray& data, bool encrypt) {
  if (key.size() != 16 || data.size() % 16 != 0) {
    return {};
  }
  return run(EVP_aes_128_ecb(), key, {}, data, encrypt);
}

QByteArray ctr(const QByteArray& key, const QByteArray& iv, const QByteArray& data) {
  if (key.size() != 16 || iv.size() != 16) {
    return {};
  }
  return run(EVP_aes_128_ctr(), key, iv, data, true);
}

QByteArray xtsSectors(const QByteArray& key, const QByteArray& data, quint64 firstSector, bool encrypt) {
  if (key.size() != 32 || data.size() % kSectorBytes != 0) {
    return {};
  }
  QByteArray out;
  out.reserve(data.size());
  for (qint64 sector = 0; sector < data.size() / kSectorBytes; ++sector) {
    QByteArray tweak(16, '\0');
    qToBigEndian<quint64>(firstSector + static_cast<quint64>(sector), tweak.data() + 8);
    const QByteArray block = run(EVP_aes_128_xts(), key, tweak,
                                 data.mid(sector * kSectorBytes, kSectorBytes), encrypt);
    if (block.size() != kSectorBytes) {
      return {};
    }
    out += block;
  }
  return out;
}
}  // namespace SwitchCrypto

QStringList SwitchTitleReader::defaultKeyFiles() {
  const QString home = QDir::homePath();
  return {home + QStringLiteral("/.config/Ryujinx/system/prod.keys"),
          home + QStringLiteral("/.var/app/io.github.ryubing.Ryujinx/config/Ryujinx/system/prod.keys"),
          home + QStringLiteral("/.var/app/org.ryujinx.Ryujinx/config/Ryujinx/system/prod.keys"),
          home + QStringLiteral("/.switch/prod.keys"),
          home + QStringLiteral("/.local/share/yuzu/keys/prod.keys")};
}

QStringList SwitchTitleReader::defaultTitleKeyFiles() {
  QStringList files;
  for (const QString& path : defaultKeyFiles()) {
    files.append(QFileInfo(path).absolutePath() + QStringLiteral("/title.keys"));
  }
  return files;
}

bool SwitchTitleReader::keysAvailable(const QStringList& keyFiles) {
  for (const QString& path : keyFiles) {
    if (QFileInfo(path).isFile()) {
      return true;
    }
  }
  return false;
}

SwitchTitleInfo SwitchTitleReader::read(const QString& romPath, const QStringList& keyFiles,
                                        const QStringList& titleKeyFiles) {
  SwitchTitleInfo info;
  const Keys keys = loadKeys(keyFiles, titleKeyFiles);
  if (!keys.valid()) {
    info.failure = QStringLiteral("no prod.keys with a header_key was found");
    return info;
  }
  QFile file(romPath);
  if (!file.open(QIODevice::ReadOnly)) {
    info.failure = QStringLiteral("could not open the dump");
    return info;
  }
  QVector<Entry> tickets;
  const QVector<Entry> ncas = ncaEntries(file, &tickets, &info.notes);
  if (ncas.isEmpty()) {
    const QByteArray head = readAt(file, 0, 0x200);
    info.failure = QStringLiteral("not an NSP or XCI dump (starts %1, at 0x100 %2)")
                       .arg(QString::fromLatin1(head.left(4).toHex()), QString::fromLatin1(head.mid(0x100, 4)));
    return info;
  }
  ControlSection control;
  if (!findControl(file, keys, ncas, tickets, &control, &info.failure, &info.notes)) {
    return info;
  }
  extract(file, control, &info);
  return info;
}

QString SwitchTitleReader::defaultCacheRoot() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/covers/switch");
}

QString SwitchTitleReader::cachedIcon(const QString& romPath, const QString& cacheKey,
                                      const QString& cacheRoot, SwitchTitleInfo* info) {
  static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]"));
  const QString safeKey = QString(cacheKey).replace(unsafe, QStringLiteral("_"));
  if (safeKey.isEmpty()) {
    return {};
  }
  const QString path = cacheRoot + QLatin1Char('/') + safeKey + QStringLiteral(".jpg");
  if (info == nullptr && QFileInfo::exists(path)) {
    return path;
  }
  const SwitchTitleInfo read = SwitchTitleReader::read(romPath);
  if (info != nullptr) {
    *info = read;
  }
  if (!read.hasIcon()) {
    return QFileInfo::exists(path) ? path : QString{};
  }
  QDir().mkpath(cacheRoot);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(read.icon) != read.icon.size() || !file.commit()) {
    return {};
  }
  return path;
}
