#include "artwork/TgaImage.h"

#include <QtEndian>

QImage decodeTgaImage(const QByteArray& bytes) {
  if (bytes.size() < 18) {
    return {};
  }
  const auto* header = reinterpret_cast<const unsigned char*>(bytes.constData());
  const int idLength = header[0];
  const int colorMapType = header[1];
  const int imageType = header[2];
  const int width = qFromLittleEndian<quint16>(header + 12);
  const int height = qFromLittleEndian<quint16>(header + 14);
  const int depth = header[16];
  const int descriptor = header[17];
  const bool runLength = imageType == 10;
  if (colorMapType != 0 || (imageType != 2 && imageType != 10) || (depth != 24 && depth != 32) ||
      width <= 0 || height <= 0 || width > 4096 || height > 4096) {
    return {};
  }
  const int bytesPerPixel = depth / 8;
  const bool topDown = (descriptor & 0x20) != 0;
  QImage image(width, height, depth == 32 ? QImage::Format_ARGB32 : QImage::Format_RGB32);
  qsizetype cursor = 18 + idLength;
  const auto readPixel = [&](qsizetype at) -> QRgb {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData()) + at;
    return depth == 32 ? qRgba(p[2], p[1], p[0], p[3]) : qRgb(p[2], p[1], p[0]);
  };
  const qint64 pixelCount = static_cast<qint64>(width) * height;
  qint64 written = 0;
  const auto put = [&](QRgb value) {
    const int index = static_cast<int>(written);
    const int x = index % width;
    const int row = index / width;
    const int y = topDown ? row : height - 1 - row;
    image.setPixel(x, y, value);
    ++written;
  };
  while (written < pixelCount) {
    if (runLength) {
      if (cursor >= bytes.size()) {
        return {};
      }
      const int packet = static_cast<unsigned char>(bytes.at(cursor++));
      const int count = (packet & 0x7F) + 1;
      if (packet & 0x80) {
        if (cursor + bytesPerPixel > bytes.size()) {
          return {};
        }
        const QRgb value = readPixel(cursor);
        cursor += bytesPerPixel;
        for (int index = 0; index < count && written < pixelCount; ++index) {
          put(value);
        }
      } else {
        for (int index = 0; index < count && written < pixelCount; ++index) {
          if (cursor + bytesPerPixel > bytes.size()) {
            return {};
          }
          put(readPixel(cursor));
          cursor += bytesPerPixel;
        }
      }
    } else {
      if (cursor + bytesPerPixel > bytes.size()) {
        return {};
      }
      put(readPixel(cursor));
      cursor += bytesPerPixel;
    }
  }
  return image;
}
