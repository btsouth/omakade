// Files and downloads that come from outside: a GameTDB cover, a Wii U archive and a Switch dump
// can each claim sizes and offsets that are not real. These cases check that such claims are
// refused before anything large is read, allocated or cached.

#include "artwork/SwitchTitleReader.h"
#include "artwork/ZArchiveReader.h"
#include "library/DolphinGameModel.h"
#include "sources/cemu/CemuScanner.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <functional>
#include <memory>
#include <optional>

namespace {
constexpr qint64 kStreamChunk = 256 * 1024;

QByteArray le32(quint32 value) { QByteArray b(4, '\0'); qToLittleEndian(value, b.data()); return b; }
QByteArray le64(quint64 value) { QByteArray b(8, '\0'); qToLittleEndian(value, b.data()); return b; }
QByteArray be32(quint32 value) { QByteArray b(4, '\0'); qToBigEndian(value, b.data()); return b; }
QByteArray be64(quint64 value) { QByteArray b(8, '\0'); qToBigEndian(value, b.data()); return b; }
QByteArray padded(QByteArray bytes, qsizetype alignment) {
  while (bytes.size() % alignment != 0) bytes.append('\0');
  return bytes;
}
QByteArray randomBytes(int count) {
  QByteArray bytes(count, '\0');
  for (char& byte : bytes) byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
  return bytes;
}
QByteArray encoded(const QImage& image, const char* format) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, format);
  return bytes;
}
QByteArray image(int width, int height, const char* format) {
  QImage picture(width, height, QImage::Format_RGB32);
  picture.fill(Qt::darkCyan);
  return encoded(picture, format);
}
bool writeFile(const QString& path, const QByteArray& contents) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}

// Answers every request on server with respond(socket) once the request headers are in.
void serve(QTcpServer* server, const std::function<void(QTcpSocket*)>& respond) {
  QObject::connect(server, &QTcpServer::newConnection, server, [server, respond] {
    while (QTcpSocket* socket = server->nextPendingConnection()) {
      auto request = std::make_shared<QByteArray>();
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, request, respond] {
        const bool answered = request->contains("\r\n\r\n");
        request->append(socket->readAll());
        if (!answered && request->contains("\r\n\r\n")) respond(socket);
      });
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
  });
}

QUrl coverUrl(const QTcpServer& server) {
  return QUrl(QStringLiteral("http://127.0.0.1:%1/wii/cover/US/GZLE01.png").arg(server.serverPort()));
}

// A ZArchive with a 64 KiB data section, one root directory and the given record table,
// laid out sparsely: the record table is never written, only claimed by the footer. The name
// table and node tree default to the root alone; treeBytes, when larger than the tree given,
// claims a longer tree whose rest is left unwritten.
bool writeArchive(const QString& path, quint64 dataSize, quint64 recordBytes,
                  const QByteArray& names = QByteArray(1, '\0'),  // the root's empty name
                  const QByteArray& tree = be32(0) + be32(1) + be32(0) + be32(0), quint64 treeBytes = 0) {
  constexpr qint64 kFooterBytes = 16 * 6 + 32 + 8 + 4 + 4;
  treeBytes = qMax<quint64>(treeBytes, tree.size());
  const quint64 recordsOffset = dataSize;
  const quint64 namesOffset = recordsOffset + recordBytes;
  const quint64 treeOffset = namesOffset + names.size();
  const quint64 footerOffset = treeOffset + treeBytes;
  const quint64 total = footerOffset + kFooterBytes;
  QByteArray footer = be64(0) + be64(dataSize) + be64(recordsOffset) + be64(recordBytes) + be64(namesOffset) +
                      be64(names.size()) + be64(treeOffset) + be64(treeBytes) + be64(0) + be64(0) + be64(0) +
                      be64(0) + QByteArray(32, '\0') + be64(total) + be32(0x61bf3a01) + be32(0x169f52d6);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || !file.resize(static_cast<qint64>(total))) {
    return false;
  }
  // A record table that fits is filled in, so the control case is a real archive.
  if (recordBytes == 40 && (!file.seek(static_cast<qint64>(recordsOffset)) ||
                            file.write(be64(0) + QByteArray(32, '\0')) != 40)) {
    return false;
  }
  return file.seek(static_cast<qint64>(namesOffset)) && file.write(names + tree) == names.size() + tree.size() &&
         file.seek(static_cast<qint64>(footerOffset)) && file.write(footer) == kFooterBytes;
}

// The layout of a synthetic NSP: one control NCA in a PFS0, as in CoreTests, with the offsets
// that matter here open to change.
struct SyntheticDump {
  quint64 romfsOffset = 0;      // the NCA FS header's RomFS offset
  quint64 nacpOffset = 0;       // control.nacp's offset in the RomFS file table
  quint64 pfsEntryOffset = 0;   // the NCA's offset in the PFS0 entry table
};

struct BuiltDump {
  QString nspPath;
  QString keysPath;
  QByteArray icon;
  quint64 romfsDataOffset = 0;
};

BuiltDump buildDump(const QTemporaryDir& temp, const SyntheticDump& layout) {
  const QByteArray headerKey = randomBytes(32);
  const QByteArray keyAreaKey = randomBytes(16);
  const QByteArray ctrKey = randomBytes(16);
  const QByteArray sectionCtr = randomBytes(8);
  BuiltDump built;
  built.keysPath = temp.filePath(QStringLiteral("prod.keys"));
  writeFile(built.keysPath, QStringLiteral("header_key = %1\nkey_area_key_application_00 = %2\n")
                                .arg(QString::fromLatin1(headerKey.toHex()), QString::fromLatin1(keyAreaKey.toHex()))
                                .toUtf8());

  built.icon = image(48, 48, "JPEG");
  QByteArray nacp(0x4000, '\0');
  const QByteArray name = QByteArrayLiteral("Synthetic Adventure");
  nacp.replace(0, name.size(), name);
  const QByteArray fileData = padded(nacp, 0x10) + padded(built.icon, 0x10);
  const auto fileEntry = [](const QByteArray& entryName, quint64 offset, quint64 size, quint32 sibling) {
    return le32(0) + le32(sibling) + le64(offset) + le64(size) + le32(0xFFFFFFFF) + le32(entryName.size()) +
           padded(entryName, 4);
  };
  const QByteArray first = fileEntry("control.nacp", layout.nacpOffset, nacp.size(), 0);
  QByteArray fileTable = fileEntry("control.nacp", layout.nacpOffset, nacp.size(), first.size());
  fileTable += fileEntry("icon_AmericanEnglish.dat", padded(nacp, 0x10).size(), built.icon.size(), 0xFFFFFFFF);
  const QByteArray dirTable = le32(0) + le32(0xFFFFFFFF) + le32(0) + le32(0xFFFFFFFF) + le32(0xFFFFFFFF) + le32(0);
  const quint64 dirHashOffset = 0x50;
  const quint64 dirTableOffset = dirHashOffset + 0x10;
  const quint64 fileHashOffset = dirTableOffset + dirTable.size();
  const quint64 fileTableOffset = fileHashOffset + 0x10;
  const quint64 dataOffset = (fileTableOffset + fileTable.size() + 0xF) & ~quint64(0xF);
  built.romfsDataOffset = dataOffset;
  QByteArray romfs = le64(0x50) + le64(dirHashOffset) + le64(0x10) + le64(dirTableOffset) + le64(dirTable.size()) +
                     le64(fileHashOffset) + le64(0x10) + le64(fileTableOffset) + le64(fileTable.size()) + le64(dataOffset);
  romfs += QByteArray(0x10, '\xFF') + dirTable + QByteArray(0x10, '\xFF') + fileTable;
  romfs = padded(padded(romfs, 0x10) + fileData, 0x200);

  QByteArray header(0xC00, '\0');
  header.replace(0x200, 4, "NCA3");
  header[0x205] = 2;  // control
  header.replace(0x240, 4, le32(6));
  header.replace(0x244, 4, le32(6 + romfs.size() / 0x200));
  QByteArray keyArea(0x40, '\0');
  keyArea.replace(0x20, 16, ctrKey);
  header.replace(0x300, 0x40, SwitchCrypto::ecb(keyAreaKey, keyArea, true));
  header[0x400 + 2] = 0;  // RomFS
  header[0x400 + 3] = 3;
  header[0x400 + 4] = 3;  // CTR
  header.replace(0x408, 4, "IVFC");
  header.replace(0x400 + 0x90, 8, le64(layout.romfsOffset));
  header.replace(0x540, 8, sectionCtr);
  QByteArray iv(16, '\0');
  for (int index = 0; index < 8; ++index) iv[index] = sectionCtr.at(7 - index);
  qToBigEndian<quint64>(0xC00 >> 4, iv.data() + 8);
  const QByteArray nca = SwitchCrypto::xtsSectors(headerKey, header, 0, true) + SwitchCrypto::ctr(ctrKey, iv, romfs);

  const QByteArray strings = padded(QByteArray("control.nca") + '\0', 0x20);
  const QByteArray nsp = QByteArray("PFS0") + le32(1) + le32(strings.size()) + le32(0) + le64(layout.pfsEntryOffset) +
                         le64(nca.size()) + le32(0) + le32(0) + strings + nca;
  built.nspPath = temp.filePath(QStringLiteral("game.nsp"));
  writeFile(built.nspPath, nsp);
  return built;
}
}  // namespace

class ParserHardeningTests final : public QObject {
  Q_OBJECT

private slots:
  void dolphinCoverAbortsOversizedResponse_data();
  void dolphinCoverAbortsOversizedResponse();
  void dolphinCoverCachesOnlyRealCovers_data();
  void dolphinCoverCachesOnlyRealCovers();
  void zarchiveRejectsOversizedRecordTable_data();
  void zarchiveRejectsOversizedRecordTable();
  void zarchiveCapsExpandedNames_data();
  void zarchiveCapsExpandedNames();
  void zarchiveRejectsTooManyNodes();
  void zarchiveListsDirectoriesAsLookupFindsThem();
  void cemuScanReadsArchiveWithManyAddOnFolders();
  void switchReaderRejectsWrappedEntryOffset();
  void switchReaderRejectsHugePartitionOffset();
  void switchReaderRejectsHugeRomfsOffsets();
};

void ParserHardeningTests::dolphinCoverAbortsOversizedResponse_data() {
  QTest::addColumn<bool>("announced");
  QTest::newRow("streamed without a length") << false;
  QTest::newRow("announced by Content-Length") << true;
}

// A server that keeps sending must be cut off once the body passes the cap, not read to the end
// and trimmed afterwards.
void ParserHardeningTests::dolphinCoverAbortsOversizedResponse() {
  QFETCH(bool, announced);
  const qint64 total = 16 * DolphinGameModel::kMaximumCoverBytes;
  qint64 sent = 0;
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost));
  serve(&server, [&sent, total, announced](QTcpSocket* socket) {
    QByteArray head = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\n";
    if (announced) head += "Content-Length: " + QByteArray::number(total) + "\r\n";
    socket->write(head + "\r\n");
    // Writes only as fast as the client reads, so `sent` is what actually went out.
    const auto more = [socket, &sent, total] {
      if (socket->state() != QAbstractSocket::ConnectedState || socket->bytesToWrite() > kStreamChunk) return;
      if (sent >= total) {
        socket->disconnectFromHost();
        return;
      }
      QByteArray chunk(kStreamChunk, 'x');
      if (sent == 0) chunk.replace(0, 8, "\x89PNG\r\n\x1a\n");
      sent += socket->write(chunk);
    };
    QObject::connect(socket, &QTcpSocket::bytesWritten, socket, more);
    more();
  });

  const QString cache = temp.filePath(QStringLiteral("covers/GZLE01.png"));
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy::NoProxy);
  QObject context;
  std::optional<bool> saved;
  DolphinGameModel::fetchCover(&network, coverUrl(server), cache, &context, [&saved](bool value) { saved = value; });
  QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 30000);
  QVERIFY(!*saved);
  QVERIFY(!QFileInfo::exists(cache));
  QVERIFY2(sent < total / 2, qPrintable(QStringLiteral("server sent %1 of %2 bytes").arg(sent).arg(total)));
}

void ParserHardeningTests::dolphinCoverCachesOnlyRealCovers_data() {
  QTest::addColumn<QByteArray>("body");
  QTest::addColumn<int>("status");
  QTest::addColumn<bool>("cached");
  const QByteArray cover = image(160, 224, "PNG");
  QTest::newRow("cover") << cover << 200 << true;
  QTest::newRow("png magic then junk") << QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(4096, 'x') << 200 << false;
  QTest::newRow("truncated png") << cover.left(cover.size() / 2) << 200 << false;
  QTest::newRow("jpeg") << image(160, 224, "JPEG") << 200 << false;
  QTest::newRow("too wide") << image(5000, 20, "PNG") << 200 << false;
  QTest::newRow("too small") << image(8, 8, "PNG") << 200 << false;
  QTest::newRow("error page") << cover << 404 << false;
}

void ParserHardeningTests::dolphinCoverCachesOnlyRealCovers() {
  QFETCH(QByteArray, body);
  QFETCH(int, status);
  QFETCH(bool, cached);
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost));
  serve(&server, [body, status](QTcpSocket* socket) {
    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Status\r\nContent-Type: image/png\r\n"
                  "Connection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
    socket->disconnectFromHost();
  });

  const QString cache = temp.filePath(QStringLiteral("covers/GZLE01.png"));
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy::NoProxy);
  QObject context;
  std::optional<bool> saved;
  DolphinGameModel::fetchCover(&network, coverUrl(server), cache, &context, [&saved](bool value) { saved = value; });
  QTRY_VERIFY_WITH_TIMEOUT(saved.has_value(), 15000);
  QCOMPARE(*saved, cached);
  QCOMPARE(QFileInfo::exists(cache), cached);
  if (cached) {
    QFile file(cache);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), body);
  }
}

void ParserHardeningTests::zarchiveRejectsOversizedRecordTable_data() {
  QTest::addColumn<quint64>("dataSize");
  QTest::addColumn<quint64>("recordBytes");
  QTest::addColumn<bool>("opens");
  // The control: the same layout with one record is a real, if empty, archive.
  QTest::newRow("one record") << quint64(64 * 1024) << quint64(40) << true;
  // About 2 GB of records behind a data section big enough to need them: over the byte cap.
  QTest::newRow("over the byte cap") << quint64(3) * 1024 * 1024 * 1024 << quint64(40) * 50'000'000 << false;
  // Under the byte cap, but far more records than 64 KiB of data could ever use.
  QTest::newRow("more records than data") << quint64(64 * 1024) << quint64(40) * 100'000 << false;
}

// The footer of a sparse file can claim a record table of any size without the file taking any
// space, so the claim is checked before the table is read or a record allocated.
void ParserHardeningTests::zarchiveRejectsOversizedRecordTable() {
  QFETCH(quint64, dataSize);
  QFETCH(quint64, recordBytes);
  QFETCH(bool, opens);
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("game.wua"));
  QVERIFY(writeArchive(path, dataSize, recordBytes));
  QCOMPARE(QFileInfo(path).size(), qint64(dataSize + recordBytes + 1 + 16 + 16 * 6 + 32 + 8 + 4 + 4));
  const auto reader = ZArchiveReader::open(path);
  QCOMPARE(reader != nullptr, opens);
  if (reader) {
    QVERIFY(reader->list(QString{}).isEmpty());
    QVERIFY(reader->readFile(QStringLiteral("meta/meta.xml"), 1024).isEmpty());
  }
}

void ParserHardeningTests::zarchiveCapsExpandedNames_data() {
  QTest::addColumn<int>("files");
  QTest::addColumn<bool>("opens");
  // The control: a hundred files sharing one name of the longest length a name can have.
  QTest::newRow("a hundred long names") << 100 << true;
  // About 128 KB of tree that would expand to 64 million characters of names, 128 MB in memory.
  QTest::newRow("thousands of long names") << 2048 << false;
}

// Each node points into the name table, and nothing stops every node pointing at the same long
// name, so a small tree could make the names it expands to take gigabytes.
void ParserHardeningTests::zarchiveCapsExpandedNames() {
  QFETCH(int, files);
  QFETCH(bool, opens);
  constexpr int kLength = 0x7FFF;  // the longest a two-byte length prefix can say
  const QByteArray names = QByteArray(1, char(0x80 | (kLength & 0x7F))) + QByteArray(1, char(kLength >> 7)) +
                           QByteArray(kLength, 'a');
  QByteArray tree = be32(0x7FFFFFFF) + be32(1) + be32(static_cast<quint32>(files)) + be32(0);  // root, no name
  for (int index = 0; index < files; ++index) {
    tree += be32(0x80000000) + be32(0) + be32(1) + be32(0);  // a one-byte file named by the long name
  }
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("game.wua"));
  QVERIFY(writeArchive(path, 64 * 1024, 40, names, tree));
  const auto reader = ZArchiveReader::open(path);
  QCOMPARE(reader != nullptr, opens);
  if (reader) {
    const QStringList listed = reader->list(QString{});
    QCOMPARE(listed.size(), files);
    QCOMPARE(listed.first(), QString(kLength, QLatin1Char('a')));
  }
}

// A tree of more nodes than any dump has is refused before it is read or a node allocated.
void ParserHardeningTests::zarchiveRejectsTooManyNodes() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("game.wua"));
  const QByteArray root = be32(0) + be32(1) + be32(0) + be32(0);
  QVERIFY(writeArchive(path, 64 * 1024, 40, QByteArray(1, '\0'), root, quint64(16) * (1024 * 1024 + 1)));
  QVERIFY(ZArchiveReader::open(path) == nullptr);
  // The same tree one node shorter is read: the rest are empty directories with no name.
  QVERIFY(writeArchive(path, 64 * 1024, 40, QByteArray(1, '\0'), root, quint64(16) * 1024 * 1024));
  const auto reader = ZArchiveReader::open(path);
  QVERIFY(reader != nullptr);
  QVERIFY(reader->list(QString{}).isEmpty());
}

namespace {
// The root's empty name and then the given names, each behind a one-byte length.
QByteArray nameTable(const QList<QByteArray>& names, QList<quint32>* offsets) {
  QByteArray table(1, '\0');
  for (const QByteArray& name : names) {
    offsets->append(static_cast<quint32>(table.size()));
    table += char(name.size()) + name;
  }
  return table;
}
QByteArray emptyDirectory(quint32 nameOffset) { return be32(nameOffset) + be32(0) + be32(0) + be32(0); }
QByteArray emptyFile(quint32 nameOffset) { return be32(0x80000000u | nameOffset) + be32(0) + be32(0) + be32(0); }
}  // namespace

// A path finds the first entry with its name, ignoring case, so a later entry with the same name
// is a directory exactly when that first one is. The one-pass listing agrees with isDirectory().
void ParserHardeningTests::zarchiveListsDirectoriesAsLookupFindsThem() {
  QList<quint32> offsets;
  const QByteArray names = nameTable({"A", "a", "b", "B", "c", "D"}, &offsets);
  const QByteArray tree = be32(0) + be32(1) + be32(6) + be32(0) + emptyDirectory(offsets[0]) + emptyFile(offsets[1]) +
                          emptyFile(offsets[2]) + emptyDirectory(offsets[3]) + emptyFile(offsets[4]) +
                          emptyDirectory(offsets[5]);
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("game.wua"));
  QVERIFY(writeArchive(path, 64 * 1024, 40, names, tree));
  const auto reader = ZArchiveReader::open(path);
  QVERIFY(reader != nullptr);
  QStringList expected;
  for (const QString& name : reader->list(QString{})) {
    if (reader->isDirectory(name)) expected.append(name);
  }
  QCOMPARE(expected, (QStringList{QStringLiteral("A"), QStringLiteral("a"), QStringLiteral("D")}));
  QCOMPARE(reader->listDirectories(QString{}), expected);
  QVERIFY(reader->listDirectories(QStringLiteral("c")).isEmpty());
}

// An archive can name a hundred thousand add-on folders ahead of its game. Checking each one by
// searching the root again took billions of name comparisons; it has to take one pass.
void ParserHardeningTests::cemuScanReadsArchiveWithManyAddOnFolders() {
  constexpr int kAddOns = 100'000;
  QList<QByteArray> folderNames;
  folderNames.reserve(kAddOns + 1);
  for (int index = 0; index < kAddOns; ++index) {
    folderNames.append(QByteArray("0005000C") + QByteArray::number(index, 16).rightJustified(8, '0').toUpper() +
                       QByteArray("_v0"));
  }
  folderNames.append(QByteArrayLiteral("00050000ABCDEF12_v0"));
  QList<quint32> offsets;
  const QByteArray names = nameTable(folderNames, &offsets);
  QByteArray tree = be32(0) + be32(1) + be32(static_cast<quint32>(folderNames.size())) + be32(0);
  for (const quint32 offset : offsets) tree += emptyDirectory(offset);

  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("game.wua"));
  QVERIFY(writeArchive(path, 64 * 1024, 40, names, tree));
  const auto reader = ZArchiveReader::open(path);
  QVERIFY(reader != nullptr);
  QCOMPARE(reader->listDirectories(QString{}).size(), kAddOns + 1);

  QStandardPaths::setTestModeEnabled(true);
  const auto restoreCache = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });
  const QString cemuRoot = temp.filePath(QStringLiteral("cemu"));
  QVERIFY(QDir().mkpath(cemuRoot));
  QVERIFY(writeFile(cemuRoot + QStringLiteral("/settings.xml"),
                    QStringLiteral("<content><GamePaths><Entry>%1</Entry></GamePaths></content>")
                        .arg(temp.path())
                        .toUtf8()));
  QElapsedTimer timer;
  timer.start();
  const CemuScanResult scan = CemuScanner::scan({cemuRoot});
  const qint64 elapsed = timer.elapsed();
  QCOMPARE(scan.games.size(), 1);
  QCOMPARE(scan.games.first().titleId, QStringLiteral("00050000ABCDEF12"));
  // One pass takes well under a second, even in a debug build on a busy machine.
  QVERIFY2(elapsed < 5000, qPrintable(QStringLiteral("scan took %1 ms").arg(elapsed)));
}

// The PFS0 entry offset is added to the data base. One that wraps around to point back at the
// start of the file is not an entry.
void ParserHardeningTests::switchReaderRejectsWrappedEntryOffset() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const quint64 dataBase = 0x10 + 0x18 + 0x20;
  const BuiltDump dump = buildDump(temp, {.pfsEntryOffset = ~dataBase + 1});
  const SwitchTitleInfo info = SwitchTitleReader::read(dump.nspPath, {dump.keysPath}, {});
  QVERIFY2(info.failure.startsWith(QStringLiteral("not an NSP or XCI dump")), qPrintable(info.failure));
  QVERIFY2(info.notes.isEmpty(), qPrintable(info.notes.join(QStringLiteral(" | "))));
  QVERIFY(!info.hasIcon());
}

void ParserHardeningTests::switchReaderRejectsHugePartitionOffset() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  QByteArray head(0x1000, '\0');
  head.replace(0x100, 4, "HEAD");
  head.replace(0x130, 8, le64(0x7FFFFFFFFFFFFFF8ull));
  const QString path = temp.filePath(QStringLiteral("game.xci"));
  QVERIFY(writeFile(path, head));
  const QString keys = temp.filePath(QStringLiteral("prod.keys"));
  QVERIFY(writeFile(keys, "header_key = " + randomBytes(32).toHex() + "\n"));
  const SwitchTitleInfo info = SwitchTitleReader::read(path, {keys}, {});
  QVERIFY2(info.failure.startsWith(QStringLiteral("not an NSP or XCI dump")), qPrintable(info.failure));
}

void ParserHardeningTests::switchReaderRejectsHugeRomfsOffsets() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());

  // The unchanged dump reads, so the cases below fail on their offsets and nothing else.
  const BuiltDump plain = buildDump(temp, {});
  const SwitchTitleInfo good = SwitchTitleReader::read(plain.nspPath, {plain.keysPath}, {});
  QVERIFY2(good.failure.isEmpty(), qPrintable(good.failure + " " + good.notes.join(" | ")));
  QCOMPARE(good.title, QStringLiteral("Synthetic Adventure"));
  QCOMPARE(good.icon, plain.icon);

  // A RomFS that starts far past the end of its section.
  const BuiltDump far = buildDump(temp, {.romfsOffset = 0x7FFFFFFFFFFFFF00ull});
  const SwitchTitleInfo farInfo = SwitchTitleReader::read(far.nspPath, {far.keysPath}, {});
  QVERIFY(!farInfo.hasIcon());
  QVERIFY(farInfo.title.isEmpty());
  QVERIFY(!farInfo.failure.isEmpty());

  // A negative RomFS offset, which would read the NCA header as if it were the RomFS.
  const BuiltDump behind = buildDump(temp, {.romfsOffset = ~quint64(0xC00) + 1});
  const SwitchTitleInfo behindInfo = SwitchTitleReader::read(behind.nspPath, {behind.keysPath}, {});
  QVERIFY(!behindInfo.hasIcon());
  QVERIFY(behindInfo.title.isEmpty());
  QVERIFY(!behindInfo.failure.isEmpty());

  // control.nacp at an offset that wraps back to the start of the RomFS: the title is not read
  // from there, and the icon next to it still is.
  const BuiltDump wrapped = buildDump(temp, {.nacpOffset = ~plain.romfsDataOffset + 1});
  const SwitchTitleInfo wrappedInfo = SwitchTitleReader::read(wrapped.nspPath, {wrapped.keysPath}, {});
  QVERIFY(wrappedInfo.title.isEmpty());
  QCOMPARE(wrappedInfo.icon, wrapped.icon);
}

QTEST_GUILESS_MAIN(ParserHardeningTests)

#include "ParserHardeningTests.moc"
