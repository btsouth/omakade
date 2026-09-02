#include "sources/battlenet/BattleNetScanner.h"

#include "sources/steam/SteamScanner.h"
#include "sources/steam/ValveKeyValues.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

namespace {
constexpr qint64 kMaximumProductDbBytes = 16 * 1024 * 1024;
constexpr qint64 kMaximumConfigBytes = 4 * 1024 * 1024;
const QString kProductDbRelative =
    QStringLiteral("/drive_c/ProgramData/Battle.net/Agent/product.db");

struct ProductCatalogEntry {
  const char* productCode;
  const char* title;
  const char* launchCode;
  const char* slug;
};

constexpr ProductCatalogEntry kCatalog[] = {
    {"s1", "StarCraft Remastered", "S1", "starcraft-remastered"},
    {"s2", "StarCraft II", "S2", "starcraft-ii"},
    {"sca", "StarCraft Anthology", "Starcraft", "starcraft"},
    {"wow", "World of Warcraft", "WoW", "world-of-warcraft"},
    {"wow_classic", "World of Warcraft Classic", "WoW_wow_classic", "world-of-warcraft-classic"},
    {"wow_classic_era", "World of Warcraft Classic Era", "WoW_wow_classic_era",
     "world-of-warcraft-classic"},
    {"wow_classic_ptr", "World of Warcraft Classic PTR", "WoW_wow_classic",
     "world-of-warcraft-classic"},
    {"wow_classic_era_ptr", "World of Warcraft Classic Era PTR", "WoW_wow_classic_era",
     "world-of-warcraft-classic"},
    {"wow_beta", "World of Warcraft Beta", "WoW", "world-of-warcraft"},
    {"pro", "Overwatch 2", "Pro", "overwatch-2"},
    {"prometheus", "Overwatch 2", "Pro", "overwatch-2"},
    {"prometheus_dev", "Overwatch 2 PTR", "Pro", "overwatch-2"},
    {"w1", "Warcraft: Orcs & Humans", "W1", "warcraft-orcs-humans"},
    {"w1r", "Warcraft I: Remastered", "W1R", "warcraft-i-remastered"},
    {"w2bn", "Warcraft II: Battle.net Edition", "W2BN", "warcraft-ii-battle-net-edition"},
    {"w2r", "Warcraft II: Remastered", "W2R", "warcraft-ii-remastered"},
    {"w3", "Warcraft III: Reforged", "W3", "warcraft-iii-reforged"},
    {"w3ROC", "Warcraft III: Reign of Chaos", "Warcraft III", "warcraft-iii-reign-of-chaos"},
    {"w3tft", "Warcraft III: The Frozen Throne", "Warcraft III", "warcraft-iii-the-frozen-throne"},
    {"wild", "Warcraft III: Reforged", "W3", "warcraft-iii-reforged"},
    {"gryphon", "Warcraft Rumble", "GRY", "warcraft-rumble"},
    {"hsb", "Hearthstone", "WTCG", "hearthstone"},
    {"wtcg", "Hearthstone", "WTCG", "hearthstone"},
    {"hero", "Heroes of the Storm", "Hero", "heroes-of-the-storm"},
    {"d2", "Diablo II", "Diablo II", "diablo-ii"},
    {"d2LOD", "Diablo II: Lord of Destruction", "Diablo II", "diablo-ii-lord-of-destruction"},
    {"osi", "Diablo II: Resurrected", "OSI", "diablo-2-ressurected"}, // Lutris slug spelling
    {"d3", "Diablo III", "D3", "diablo-iii"},
    {"d3cn", "Diablo III", "D3CN", "diablo-iii"},
    {"fenris", "Diablo IV", "Fen", "diablo-iv"},
    {"fenris_beta", "Diablo IV Beta", "Fen", "diablo-iv"},
    {"fenris_ptr", "Diablo IV PTR", "Fen", "diablo-iv"},
    {"anbs", "Diablo Immortal", "ANBS", "diablo-immortal"},
    {"viper", "Call of Duty: Black Ops 4", "VIPR", "call-of-duty-black-ops-4"},
    {"vipr", "Call of Duty: Black Ops 4", "VIPR", "call-of-duty-black-ops-4"},
    {"odin", "Call of Duty: Modern Warfare", "ODIN", "call-of-duty-modern-warfare"},
    {"lazarus", "Call of Duty: MW2 Campaign Remastered", "LAZR",
     "call-of-duty-modern-warfare-2-campaign-remastered"},
    {"lazr", "Call of Duty: MW2 Campaign Remastered", "LAZR",
     "call-of-duty-modern-warfare-2-campaign-remastered"},
    {"zeus", "Call of Duty: Black Ops Cold War", "ZEUS", "call-of-duty-black-ops-cold-war"},
    {"auks", "Call of Duty: Modern Warfare II", "AUKS", "call-of-duty-modern-warfare-ii"},
    {"codhq", "Call of Duty HQ", "CODHQ", "call-of-duty-hq"},
    {"fore", "Call of Duty: Vanguard", "FORE", "call-of-duty-vanguard"},
    {"rtro", "Blizzard Arcade Collection", "RTRO", "blizzard-arcade-collection"},
    {"wlby", "Crash Bandicoot 4: It's About Time", "WLBY", "crash-bandicoot-4-its-about-time"},
};

QString cleanPath(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool readVarint(const QByteArray& data, int* offset, quint64* value) {
  quint64 result = 0;
  int shift = 0;
  while (*offset < data.size() && shift <= 63) {
    const auto byte = static_cast<unsigned char>(data.at((*offset)++));
    result |= static_cast<quint64>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) {
      *value = result;
      return true;
    }
    shift += 7;
  }
  return false;
}

bool readLengthDelimited(const QByteArray& data, int* offset, QByteArray* value) {
  quint64 length = 0;
  if (!readVarint(data, offset, &length) || length > static_cast<quint64>(data.size() - *offset)) {
    return false;
  }
  *value = data.mid(*offset, static_cast<int>(length));
  *offset += static_cast<int>(length);
  return true;
}

bool skipField(const QByteArray& data, int* offset, int wireType) {
  switch (wireType) {
  case 0: {
    quint64 unused = 0;
    return readVarint(data, offset, &unused);
  }
  case 1:
    if (*offset + 8 > data.size()) {
      return false;
    }
    *offset += 8;
    return true;
  case 2: {
    QByteArray unused;
    return readLengthDelimited(data, offset, &unused);
  }
  case 5:
    if (*offset + 4 > data.size()) {
      return false;
    }
    *offset += 4;
    return true;
  default:
    return false;
  }
}

bool nextField(const QByteArray& data, int* offset, int* field, int* wireType) {
  quint64 key = 0;
  if (!readVarint(data, offset, &key) || key > 0xFFFFFFFULL) {
    return false;
  }
  *field = static_cast<int>(key >> 3);
  *wireType = static_cast<int>(key & 7);
  return *field > 0;
}

bool readStringField(const QByteArray& data, int* offset, QString* value) {
  QByteArray bytes;
  if (!readLengthDelimited(data, offset, &bytes)) {
    return false;
  }
  *value = QString::fromUtf8(bytes);
  return true;
}

void writeVarint(QByteArray* out, quint64 value) {
  while (value >= 0x80) {
    out->append(static_cast<char>((value & 0x7F) | 0x80));
    value >>= 7;
  }
  out->append(static_cast<char>(value));
}

void writeKey(QByteArray* out, int field, int wireType) {
  writeVarint(out, (static_cast<quint64>(field) << 3) | static_cast<quint64>(wireType));
}

void writeString(QByteArray* out, int field, const QString& value) {
  const QByteArray utf8 = value.toUtf8();
  writeKey(out, field, 2);
  writeVarint(out, static_cast<quint64>(utf8.size()));
  out->append(utf8);
}

void writeBool(QByteArray* out, int field, bool value) {
  writeKey(out, field, 0);
  writeVarint(out, value ? 1 : 0);
}

void writeMessage(QByteArray* out, int field, const QByteArray& message) {
  writeKey(out, field, 2);
  writeVarint(out, static_cast<quint64>(message.size()));
  out->append(message);
}

bool parseBaseProductState(const QByteArray& data, bool* installed, bool* playable) {
  int offset = 0;
  while (offset < data.size()) {
    int field = 0;
    int wireType = 0;
    if (!nextField(data, &offset, &field, &wireType)) {
      return false;
    }
    if (wireType == 0 && (field == 1 || field == 2)) {
      quint64 value = 0;
      if (!readVarint(data, &offset, &value)) {
        return false;
      }
      if (field == 1) {
        *installed = value != 0;
      } else {
        *playable = value != 0;
      }
    } else if (!skipField(data, &offset, wireType)) {
      return false;
    }
  }
  return true;
}

bool parseCachedProductState(const QByteArray& data, bool* installed, bool* playable) {
  int offset = 0;
  while (offset < data.size()) {
    int field = 0;
    int wireType = 0;
    if (!nextField(data, &offset, &field, &wireType)) {
      return false;
    }
    if (field == 1 && wireType == 2) {
      QByteArray nested;
      if (!readLengthDelimited(data, &offset, &nested) ||
          !parseBaseProductState(nested, installed, playable)) {
        return false;
      }
    } else if (!skipField(data, &offset, wireType)) {
      return false;
    }
  }
  return true;
}

bool parseUserSettings(const QByteArray& data, QString* installPath) {
  int offset = 0;
  while (offset < data.size()) {
    int field = 0;
    int wireType = 0;
    if (!nextField(data, &offset, &field, &wireType)) {
      return false;
    }
    if (field == 1 && wireType == 2) {
      if (!readStringField(data, &offset, installPath)) {
        return false;
      }
    } else if (!skipField(data, &offset, wireType)) {
      return false;
    }
  }
  return true;
}

bool parseProductInstall(const QByteArray& data, BattleNetProductInstall* install) {
  int offset = 0;
  while (offset < data.size()) {
    int field = 0;
    int wireType = 0;
    if (!nextField(data, &offset, &field, &wireType)) {
      return false;
    }
    if (wireType == 2 && field == 1) {
      if (!readStringField(data, &offset, &install->uid)) {
        return false;
      }
    } else if (wireType == 2 && field == 2) {
      if (!readStringField(data, &offset, &install->productCode)) {
        return false;
      }
    } else if (wireType == 2 && field == 3) {
      QByteArray nested;
      if (!readLengthDelimited(data, &offset, &nested) ||
          !parseUserSettings(nested, &install->installPath)) {
        return false;
      }
    } else if (wireType == 2 && field == 4) {
      QByteArray nested;
      if (!readLengthDelimited(data, &offset, &nested) ||
          !parseCachedProductState(nested, &install->installed, &install->playable)) {
        return false;
      }
    } else if (!skipField(data, &offset, wireType)) {
      return false;
    }
  }
  return !install->productCode.isEmpty() || !install->uid.isEmpty();
}

const ProductCatalogEntry* catalogEntry(const QString& productCode) {
  for (const ProductCatalogEntry& entry : kCatalog) {
    if (productCode.compare(QLatin1String(entry.productCode), Qt::CaseInsensitive) == 0) {
      return &entry;
    }
  }
  return nullptr;
}

QString unixPathFromWindows(const QString& prefix, const QString& windowsPath) {
  QString path = windowsPath.trimmed();
  path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (path.size() < 3 || path.at(1) != QLatin1Char(':')) {
    return {};
  }
  const QChar drive = path.at(0).toLower();
  if (drive < QLatin1Char('a') || drive > QLatin1Char('z')) {
    return {};
  }
  const QString rest = path.mid(2);
  const QString dosDevice = prefix + QStringLiteral("/dosdevices/") + drive + QLatin1Char(':');
  if (QFileInfo::exists(dosDevice)) {
    return QDir::cleanPath(QFileInfo(dosDevice).canonicalFilePath() + rest);
  }
  if (drive == QLatin1Char('c')) {
    return QDir::cleanPath(prefix + QStringLiteral("/drive_c") + rest);
  }
  return {};
}

QString firstArtwork(const QString& directory, const QStringList& names) {
  if (directory.isEmpty()) {
    return {};
  }
  for (const QString& name : names) {
    const QString candidate = directory + QLatin1Char('/') + name;
    if (QFileInfo(candidate).isFile()) {
      return candidate;
    }
  }
  return {};
}

qint64 unixFromBattleNetTimestamp(qint64 value) {
  if (value <= 0) {
    return 0;
  }
  // Windows FILETIME (100-ns ticks since 1601).
  if (value > 10'000'000'000'000'000LL) {
    const qint64 unix = (value / 10'000'000LL) - 11'644'473'600LL;
    return unix > 0 ? unix : 0;
  }
  // Milliseconds.
  if (value > 10'000'000'000LL) {
    return value / 1000;
  }
  return value;
}

QHash<QString, qint64> readLastPlayed(const QString& prefix) {
  QHash<QString, qint64> played;
  const QDir users(prefix + QStringLiteral("/drive_c/users"));
  const QStringList accounts = users.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString& account : accounts) {
    const QString path = users.absoluteFilePath(
        account + QStringLiteral("/AppData/Roaming/Battle.net/Battle.net.config"));
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly) || file.size() > kMaximumConfigBytes) {
      continue;
    }
    const QJsonObject games =
        QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("Games")).toObject();
    for (auto iterator = games.begin(); iterator != games.end(); ++iterator) {
      const qint64 stamp = unixFromBattleNetTimestamp(
          iterator.value().toObject().value(QStringLiteral("LastPlayed")).toVariant().toLongLong());
      if (stamp > 0) {
        played[iterator.key().toLower()] = qMax(played.value(iterator.key().toLower()), stamp);
      }
    }
  }
  return played;
}

QString detectRunner(const QString& prefix) {
  if (QFileInfo(prefix + QStringLiteral("/bottle.yml")).isFile()) {
    return QStringLiteral("bottles");
  }
  const QFileInfo info(prefix);
  if (info.fileName() == QStringLiteral("pfx") &&
      QFileInfo(info.dir().absoluteFilePath(QStringLiteral("version"))).isFile()) {
    return QStringLiteral("proton");
  }
  return QStringLiteral("wine");
}

bool detectFlatpak(const QString& prefix) {
  return prefix.contains(QStringLiteral("/.var/app/com.usebottles.bottles/")) ||
         prefix.contains(QStringLiteral("/.var/app/com.valvesoftware.Steam/"));
}

void appendPrefixIfPresent(const QString& prefix, QStringList* prefixes) {
  const QString cleaned = cleanPath(prefix);
  if (cleaned.isEmpty() || prefixes->contains(cleaned)) {
    return;
  }
  if (QFileInfo(cleaned + kProductDbRelative).isFile()) {
    prefixes->append(cleaned);
  }
}

void appendChildPrefixes(const QString& parent, QStringList* prefixes) {
  QDir directory(parent);
  if (!directory.exists()) {
    return;
  }
  const QStringList children = directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString& child : children) {
    appendPrefixIfPresent(directory.absoluteFilePath(child), prefixes);
  }
}

QStringList steamLibraryRoots() {
  QStringList libraries;
  for (const QString& steamRoot : SteamScanner::discoverSteamRoots()) {
    libraries.append(steamRoot);
    QString libraryFile = steamRoot + QStringLiteral("/config/libraryfolders.vdf");
    if (!QFileInfo::exists(libraryFile)) {
      libraryFile = steamRoot + QStringLiteral("/steamapps/libraryfolders.vdf");
    }
    ValveKeyValues root;
    if (!ValveKeyValuesParser::parseFile(libraryFile, &root)) {
      continue;
    }
    const ValveKeyValues* folders = root.object(QStringLiteral("libraryfolders"));
    if (folders == nullptr) {
      folders = &root;
    }
    for (auto iterator = folders->values.cbegin(); iterator != folders->values.cend(); ++iterator) {
      bool numeric = false;
      iterator.key().toInt(&numeric);
      if (numeric && !iterator.value().isEmpty()) {
        libraries.append(cleanPath(iterator.value()));
      }
    }
    for (auto iterator = folders->objects.cbegin(); iterator != folders->objects.cend();
         ++iterator) {
      bool numeric = false;
      iterator.key().toInt(&numeric);
      const QString path = iterator.value().value(QStringLiteral("path"));
      if (numeric && !path.isEmpty()) {
        libraries.append(cleanPath(path));
      }
    }
  }
  libraries.removeDuplicates();
  return libraries;
}
} // namespace

QString BattleNetScanner::titleForProduct(const QString& productCode) {
  if (const ProductCatalogEntry* entry = catalogEntry(productCodeFromId(productCode))) {
    return QString::fromUtf8(entry->title);
  }
  QString title = productCode.trimmed();
  title.replace(QLatin1Char('_'), QLatin1Char(' '));
  if (!title.isEmpty()) {
    title[0] = title[0].toUpper();
  }
  return title;
}

QString BattleNetScanner::productCodeFromId(const QString& id) {
  const QString trimmed = id.trimmed();
  const qsizetype separator = trimmed.indexOf(QLatin1Char('@'));
  return separator < 0 ? trimmed : trimmed.left(separator);
}

QString BattleNetScanner::gameIdFor(const QString& productCode, const QString& prefix) {
  const QString product = productCodeFromId(productCode);
  const QByteArray digest =
      QCryptographicHash::hash(cleanPath(prefix).toUtf8(), QCryptographicHash::Sha256).toHex();
  return product + QLatin1Char('@') + QString::fromLatin1(digest.left(8));
}

QString BattleNetScanner::launchCodeForProduct(const QString& productCode) {
  const QString product = productCodeFromId(productCode);
  if (const ProductCatalogEntry* entry = catalogEntry(product)) {
    return QString::fromUtf8(entry->launchCode);
  }
  return product;
}

QString BattleNetScanner::slugForProduct(const QString& productCode) {
  if (const ProductCatalogEntry* entry = catalogEntry(productCodeFromId(productCode));
      entry != nullptr && entry->slug != nullptr && entry->slug[0] != '\0') {
    return QString::fromUtf8(entry->slug);
  }
  return {};
}

QUrl BattleNetScanner::coverUrl(const QString& productCode) {
  const QString slug = slugForProduct(productCode);
  return slug.isEmpty()
             ? QUrl{}
             : QUrl(QStringLiteral("https://lutris.net/games/cover/%1.jpg").arg(slug));
}

QUrl BattleNetScanner::heroUrl(const QString& productCode) {
  const QString slug = slugForProduct(productCode);
  return slug.isEmpty()
             ? QUrl{}
             : QUrl(QStringLiteral("https://lutris.net/games/banner/%1.jpg").arg(slug));
}

bool BattleNetScanner::isToolProduct(const QString& productCode) {
  return productCode.compare(QStringLiteral("agent"), Qt::CaseInsensitive) == 0 ||
         productCode.compare(QStringLiteral("bna"), Qt::CaseInsensitive) == 0;
}

QByteArray BattleNetScanner::encodeProductDb(const QVector<BattleNetProductInstall>& installs) {
  QByteArray database;
  for (const BattleNetProductInstall& install : installs) {
    QByteArray settings;
    writeString(&settings, 1, install.installPath);
    QByteArray state;
    writeBool(&state, 1, install.installed);
    writeBool(&state, 2, install.playable);
    QByteArray cached;
    writeMessage(&cached, 1, state);
    QByteArray product;
    writeString(&product, 1, install.uid.isEmpty() ? install.productCode : install.uid);
    writeString(&product, 2, install.productCode);
    writeMessage(&product, 3, settings);
    writeMessage(&product, 4, cached);
    writeMessage(&database, 1, product);
  }
  return database;
}

QVector<BattleNetProductInstall> BattleNetScanner::decodeProductDb(const QByteArray& data,
                                                                   bool* ok) {
  QVector<BattleNetProductInstall> installs;
  int offset = 0;
  bool valid = true;
  while (valid && offset < data.size()) {
    int field = 0;
    int wireType = 0;
    if (!nextField(data, &offset, &field, &wireType)) {
      valid = false;
      break;
    }
    if (field == 1 && wireType == 2) {
      QByteArray nested;
      BattleNetProductInstall install;
      if (!readLengthDelimited(data, &offset, &nested) || !parseProductInstall(nested, &install)) {
        valid = false;
        break;
      }
      installs.append(install);
    } else if (!skipField(data, &offset, wireType)) {
      valid = false;
      break;
    }
  }
  if (ok != nullptr) {
    *ok = valid;
  }
  return valid ? installs : QVector<BattleNetProductInstall>{};
}

QStringList BattleNetScanner::discoverPrefixes() {
  const QString home = QDir::homePath();
  const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  QStringList prefixes;
  appendPrefixIfPresent(home + QStringLiteral("/.wine"), &prefixes);
  appendChildPrefixes(data + QStringLiteral("/wineprefixes"), &prefixes);
  appendChildPrefixes(home + QStringLiteral("/.local/share/wineprefixes"), &prefixes);
  appendChildPrefixes(data + QStringLiteral("/bottles/bottles"), &prefixes);
  appendChildPrefixes(
      home + QStringLiteral("/.var/app/com.usebottles.bottles/data/bottles/bottles"), &prefixes);
  appendChildPrefixes(home + QStringLiteral("/Games"), &prefixes);
  for (const QString& library : steamLibraryRoots()) {
    QDir compat(library + QStringLiteral("/steamapps/compatdata"));
    const QStringList apps = compat.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& app : apps) {
      appendPrefixIfPresent(compat.absoluteFilePath(app + QStringLiteral("/pfx")), &prefixes);
    }
  }
  prefixes.removeDuplicates();
  return prefixes;
}

BattleNetScanResult BattleNetScanner::scan(const QStringList& prefixes) {
  BattleNetScanResult result;
  QSet<QString> imported;
  for (const QString& prefix : prefixes) {
    const QString productDbPath = prefix + kProductDbRelative;
    if (!QFileInfo(productDbPath).isFile()) {
      continue;
    }
    result.prefixes.append(cleanPath(prefix));
    QFile file(productDbPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumProductDbBytes) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1").arg(productDbPath));
      continue;
    }
    bool ok = false;
    const QVector<BattleNetProductInstall> installs = decodeProductDb(file.readAll(), &ok);
    if (!ok) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not parse %1").arg(productDbPath));
      continue;
    }
    const QHash<QString, qint64> lastPlayed = readLastPlayed(prefix);
    const QString runner = detectRunner(prefix);
    const bool flatpak = detectFlatpak(prefix);
    for (const BattleNetProductInstall& install : installs) {
      const QString productId = install.productCode.trimmed();
      const QString gameId = gameIdFor(productId, prefix);
      if (productId.isEmpty() || isToolProduct(productId) || imported.contains(gameId) ||
          !(install.installed || install.playable)) {
        continue;
      }
      const QString installPath = unixPathFromWindows(prefix, install.installPath);
      if (installPath.isEmpty()) {
        continue;
      }
      const QString title = titleForProduct(productId);
      result.games.append(
          {.gameId = gameId,
           .productId = productId,
           .title = title,
           .launchCode = launchCodeForProduct(productId),
           .installPath = installPath,
           .winePrefix = cleanPath(prefix),
           .runner = runner,
           .coverPath = firstArtwork(
               installPath, {QStringLiteral("cover.png"), QStringLiteral("cover.jpg"),
                             QStringLiteral("cover.webp"), QStringLiteral("cover.jpeg"),
                             QStringLiteral("library_600x900.jpg"),
                             QStringLiteral("library_600x900.png")}),
           .heroPath = firstArtwork(
               installPath, {QStringLiteral("library_hero.png"), QStringLiteral("library_hero.jpg"),
                             QStringLiteral("header.jpg"), QStringLiteral("header.png")}),
           .lastPlayed = qMax(lastPlayed.value(productId.toLower()),
                              lastPlayed.value(launchCodeForProduct(productId).toLower())),
           .flatpak = flatpak});
      imported.insert(gameId);
    }
  }
  return result;
}
