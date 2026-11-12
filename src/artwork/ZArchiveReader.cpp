#include "artwork/ZArchiveReader.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QtEndian>

#include <zstd.h>

namespace {
constexpr quint32 kMagic = 0x169f52d6;
constexpr quint32 kVersion1 = 0x61bf3a01;
constexpr qint64 kFooterBytes = 16 * 6 + 32 + 8 + 4 + 4;
constexpr quint64 kBlockBytes = 64 * 1024;
constexpr int kEntriesPerRecord = 16;
constexpr quint64 kRecordBytes = 8 + 2 * kEntriesPerRecord;
// One record covers 16 blocks, 1 MiB of game data, so this is room for well over a terabyte.
constexpr quint64 kMaximumRecordBytes = 64 * 1024 * 1024;
constexpr quint64 kMaximumNameBytes = 64 * 1024 * 1024;
// A whole game with its update and DLC has well under a hundred thousand files and folders.
constexpr quint64 kMaximumNodes = 1024 * 1024;
constexpr quint64 kMaximumTreeBytes = kMaximumNodes * 16;
// Every node can point at the same long name, so the names as read are capped all together, not
// only the table they come from. This is many times the names of any real dump.
constexpr qsizetype kMaximumNameCharacters = 16 * 1024 * 1024;

QString nodeName(const QByteArray& names, quint32 offset) {
  if (offset == 0x7FFFFFFF || offset >= static_cast<quint32>(names.size())) {
    return {};
  }
  quint32 length = static_cast<unsigned char>(names.at(offset)) & 0x7F;
  if (static_cast<unsigned char>(names.at(offset)) & 0x80) {
    if (offset + 1 >= static_cast<quint32>(names.size())) {
      return {};
    }
    length |= static_cast<quint32>(static_cast<unsigned char>(names.at(offset + 1))) << 7;
    offset += 2;
  } else {
    offset += 1;
  }
  if (length > static_cast<quint32>(names.size()) - offset) {
    return {};
  }
  return QString::fromLatin1(names.constData() + offset, static_cast<int>(length));
}
}  // namespace

std::unique_ptr<ZArchiveReader> ZArchiveReader::open(const QString& path) {
  std::unique_ptr<ZArchiveReader> reader(new ZArchiveReader);
  reader->m_file.setFileName(path);
  const qint64 fileSize = QFileInfo(path).size();
  if (fileSize <= kFooterBytes || !reader->m_file.open(QIODevice::ReadOnly) ||
      !reader->m_file.seek(fileSize - kFooterBytes)) {
    return nullptr;
  }
  const QByteArray footer = reader->m_file.read(kFooterBytes);
  if (footer.size() != kFooterBytes) {
    return nullptr;
  }
  const char* f = footer.constData();
  const auto magic = qFromBigEndian<quint32>(f + kFooterBytes - 4);
  const auto version = qFromBigEndian<quint32>(f + kFooterBytes - 8);
  const auto totalSize = qFromBigEndian<quint64>(f + kFooterBytes - 16);
  if (magic != kMagic || version != kVersion1 || totalSize != static_cast<quint64>(fileSize)) {
    return nullptr;
  }
  struct Section {
    quint64 offset;
    quint64 size;
  };
  const auto section = [&](int index) {
    return Section{qFromBigEndian<quint64>(f + index * 16), qFromBigEndian<quint64>(f + index * 16 + 8)};
  };
  const Section data = section(0);
  const Section records = section(1);
  const Section names = section(2);
  const Section tree = section(3);
  const auto within = [fileSize](const Section& s) {
    return s.size <= static_cast<quint64>(fileSize) && s.offset <= static_cast<quint64>(fileSize) - s.size;
  };
  // Every block takes at least one byte of the data section, so a record table longer than the
  // data could need is not a real archive. The caps apply before any section is read: a sparse
  // file can claim any size without taking any space.
  const quint64 recordCount = records.size / kRecordBytes;
  const quint64 maximumRecords = data.size / kEntriesPerRecord + 1;
  if (!within(data) || !within(records) || !within(names) || !within(tree) ||
      records.size % kRecordBytes != 0 || records.size > kMaximumRecordBytes ||
      recordCount > maximumRecords || tree.size % 16 != 0 || tree.size == 0 ||
      names.size > kMaximumNameBytes || tree.size > kMaximumTreeBytes) {
    return nullptr;
  }
  reader->m_dataOffset = data.offset;
  reader->m_dataSize = data.size;

  const auto readSection = [&](const Section& s) {
    return reader->m_file.seek(static_cast<qint64>(s.offset)) ? reader->m_file.read(static_cast<qint64>(s.size))
                                                              : QByteArray{};
  };
  reader->m_records = readSection(records);
  const QByteArray nameBytes = readSection(names);
  const QByteArray treeBytes = readSection(tree);
  if (static_cast<quint64>(reader->m_records.size()) != records.size ||
      static_cast<quint64>(nameBytes.size()) != names.size ||
      static_cast<quint64>(treeBytes.size()) != tree.size) {
    return nullptr;
  }
  const int nodeCount = treeBytes.size() / 16;
  reader->m_nodes.reserve(nodeCount);
  qsizetype nameCharacters = 0;
  for (int index = 0; index < nodeCount; ++index) {
    const char* n = treeBytes.constData() + index * 16;
    const auto flags = qFromBigEndian<quint32>(n);
    const auto a = qFromBigEndian<quint32>(n + 4);
    const auto b = qFromBigEndian<quint32>(n + 8);
    const auto c = qFromBigEndian<quint32>(n + 12);
    Node node;
    node.name = nodeName(nameBytes, flags & 0x7FFFFFFF);
    nameCharacters += node.name.size();
    if (nameCharacters > kMaximumNameCharacters) {
      return nullptr;
    }
    node.isFile = (flags & 0x80000000) != 0;
    if (node.isFile) {
      node.offset = a | (static_cast<quint64>(c & 0xFFFF) << 32);
      node.size = b | (static_cast<quint64>(c & 0xFFFF0000) << 16);
    } else {
      node.firstChild = a;
      node.childCount = b;
    }
    reader->m_nodes.append(node);
  }
  if (reader->m_nodes.first().isFile || !reader->m_nodes.first().name.isEmpty()) {
    return nullptr;
  }
  return reader;
}

int ZArchiveReader::lookup(const QString& path) const {
  int current = 0;
  const QStringList parts = path.split(QRegularExpression(QStringLiteral("[/\\\\]+")), Qt::SkipEmptyParts);
  for (const QString& part : parts) {
    const Node& node = m_nodes.at(current);
    if (node.isFile) {
      return -1;
    }
    int match = -1;
    const quint64 end = qMin<quint64>(quint64(node.firstChild) + node.childCount, quint64(m_nodes.size()));
    for (quint64 child = node.firstChild; child < end; ++child) {
      if (m_nodes.at(static_cast<int>(child)).name.compare(part, Qt::CaseInsensitive) == 0) {
        match = static_cast<int>(child);
        break;
      }
    }
    if (match < 0) {
      return -1;
    }
    current = match;
  }
  return current;
}

QStringList ZArchiveReader::list(const QString& directory) const {
  const int index = lookup(directory);
  QStringList names;
  if (index < 0 || m_nodes.at(index).isFile) {
    return names;
  }
  const Node& node = m_nodes.at(index);
  const quint64 end = qMin<quint64>(quint64(node.firstChild) + node.childCount, quint64(m_nodes.size()));
  for (quint64 child = node.firstChild; child < end; ++child) {
    names.append(m_nodes.at(static_cast<int>(child)).name);
  }
  return names;
}

QStringList ZArchiveReader::listDirectories(const QString& directory) const {
  const int index = lookup(directory);
  QStringList names;
  if (index < 0 || m_nodes.at(index).isFile) {
    return names;
  }
  const Node& node = m_nodes.at(index);
  const quint64 end = qMin<quint64>(quint64(node.firstChild) + node.childCount, quint64(m_nodes.size()));
  // Whether the first child with each name, ignoring case, is a directory: lookup() stops there.
  QHash<QString, bool> firstIsDirectory;
  for (quint64 child = node.firstChild; child < end; ++child) {
    const Node& entry = m_nodes.at(static_cast<int>(child));
    const QString key = entry.name.toCaseFolded();
    auto first = firstIsDirectory.constFind(key);
    if (first == firstIsDirectory.cend()) {
      first = firstIsDirectory.insert(key, !entry.isFile);
    }
    if (first.value()) {
      names.append(entry.name);
    }
  }
  return names;
}

bool ZArchiveReader::isDirectory(const QString& path) const {
  const int index = lookup(path);
  return index >= 0 && !m_nodes.at(index).isFile;
}

qint64 ZArchiveReader::fileSize(const QString& path) const {
  const int index = lookup(path);
  return index >= 0 && m_nodes.at(index).isFile ? static_cast<qint64>(m_nodes.at(index).size) : -1;
}

bool ZArchiveReader::readBlock(quint64 blockIndex, QByteArray* block) {
  const quint64 recordIndex = blockIndex / kEntriesPerRecord;
  const int subIndex = static_cast<int>(blockIndex % kEntriesPerRecord);
  if (recordIndex >= static_cast<quint64>(m_records.size()) / kRecordBytes) {
    return false;
  }
  const char* record = m_records.constData() + recordIndex * kRecordBytes;
  const auto blockSize = [record](int entry) {
    return static_cast<quint64>(qFromBigEndian<quint16>(record + 8 + entry * 2)) + 1;
  };
  quint64 offset = qFromBigEndian<quint64>(record);
  // The base offset comes from the file; check it before adding to it so it cannot wrap.
  if (offset > m_dataSize) {
    return false;
  }
  for (int index = 0; index < subIndex; ++index) {
    offset += blockSize(index);
  }
  const quint64 compressedSize = blockSize(subIndex);
  if (offset > m_dataSize || compressedSize > m_dataSize - offset ||
      !m_file.seek(static_cast<qint64>(m_dataOffset + offset))) {
    return false;
  }
  const QByteArray raw = m_file.read(static_cast<qint64>(compressedSize));
  if (static_cast<quint64>(raw.size()) != compressedSize) {
    return false;
  }
  if (compressedSize == kBlockBytes) {
    *block = raw;  // stored uncompressed
    return true;
  }
  block->resize(static_cast<qsizetype>(kBlockBytes));
  const size_t produced = ZSTD_decompress(block->data(), kBlockBytes, raw.constData(), raw.size());
  return !ZSTD_isError(produced) && produced == kBlockBytes;
}

QByteArray ZArchiveReader::readFile(const QString& path, qint64 maximumBytes) {
  const int index = lookup(path);
  if (index < 0 || !m_nodes.at(index).isFile) {
    return {};
  }
  const Node& node = m_nodes.at(index);
  if (node.size == 0 || static_cast<qint64>(node.size) > maximumBytes) {
    return {};
  }
  QByteArray out;
  out.reserve(static_cast<qsizetype>(node.size));
  quint64 cursor = node.offset;
  quint64 remaining = node.size;
  QByteArray block;
  while (remaining > 0) {
    const quint64 blockIndex = cursor / kBlockBytes;
    const quint64 blockOffset = cursor % kBlockBytes;
    const quint64 step = qMin(remaining, kBlockBytes - blockOffset);
    if (!readBlock(blockIndex, &block)) {
      return {};
    }
    out.append(block.constData() + blockOffset, static_cast<qsizetype>(step));
    cursor += step;
    remaining -= step;
  }
  return out;
}
