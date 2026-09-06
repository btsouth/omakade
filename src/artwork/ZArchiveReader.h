#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

// Read-only access to ZArchive containers (.wua Wii U dumps): zstd-compressed
// 64 KiB blocks behind a directory tree, all big-endian. Only what is needed to
// pull a title's metadata and icon out of a dump.
class ZArchiveReader final {
public:
  [[nodiscard]] static std::unique_ptr<ZArchiveReader> open(const QString& path);

  [[nodiscard]] QStringList list(const QString& directory) const;  // names in a directory
  [[nodiscard]] bool isDirectory(const QString& path) const;
  [[nodiscard]] qint64 fileSize(const QString& path) const;
  // Reads a whole file, capped at maximumBytes. Empty when missing or too large.
  [[nodiscard]] QByteArray readFile(const QString& path, qint64 maximumBytes);

private:
  struct Node {
    QString name;
    bool isFile = false;
    quint64 offset = 0;      // file: uncompressed offset in the data stream
    quint64 size = 0;        // file: size
    quint32 firstChild = 0;  // directory: index of first child node
    quint32 childCount = 0;
  };
  struct OffsetRecord {
    quint64 baseOffset = 0;
    quint16 sizes[16] = {};
  };

  ZArchiveReader() = default;
  [[nodiscard]] int lookup(const QString& path) const;
  [[nodiscard]] bool readBlock(quint64 blockIndex, QByteArray* block);

  QFile m_file;
  QVector<Node> m_nodes;
  QVector<OffsetRecord> m_records;
  quint64 m_dataOffset = 0;
  quint64 m_dataSize = 0;
};
