#include "artwork/CoverImageProvider.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QUrl>

namespace {

// A card asks for the size it needs, so the same artwork at two sizes is two entries.
QString cacheKey(const QString& id, const QSize& requestedSize) {
  return id + QLatin1Char('|') + QString::number(requestedSize.width()) + QLatin1Char('x') +
         QString::number(requestedSize.height());
}

// A card marks what kind of artwork it is asking for: "f/..." is a file on disk and "q/..." is
// bundled console art. Anything else is refused rather than guessed at.
QString readablePath(const QString& id) {
  if (id.startsWith(QLatin1String("f/")))
    return id.mid(1);
  if (id.startsWith(QLatin1String("q/")))
    return QLatin1Char(':') + id.mid(1);
  return {};
}

} // namespace

CoverImageProvider::CoverImageProvider(int limitMegabytes)
    : QQuickImageProvider(QQuickImageProvider::Image) {
  m_cache.setMaxCost(qMax(1, limitMegabytes) * 1024 * 1024);
}

QImage CoverImageProvider::requestImage(const QString& id, QSize* size,
                                        const QSize& requestedSize) {
  const QString key = cacheKey(id, requestedSize);
  {
    QMutexLocker locked(&m_lock);
    if (const QImage* cached = m_cache.object(key)) {
      ++m_hits;
      if (size != nullptr)
        *size = cached->size();
      return *cached;
    }
  }

  const QString path = readablePath(id);
  QImage image;
  if (!path.isEmpty()) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize natural = reader.size();
    if (natural.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0) {
      // Decode straight to the size the card wants, keeping the artwork's own proportions so
      // the card can decide how to fit it.
      QSize target = natural;
      target.scale(requestedSize, Qt::KeepAspectRatio);
      if (!target.isEmpty() && target.width() < natural.width())
        reader.setScaledSize(target);
    }
    image = reader.read();
  }
  if (size != nullptr)
    *size = image.size();
  if (image.isNull())
    return image;

  QMutexLocker locked(&m_lock);
  ++m_misses;
  const qsizetype cost = image.sizeInBytes();
  if (cost > 0 && cost < m_cache.maxCost())
    m_cache.insert(key, new QImage(image), int(cost));
  return image;
}

int CoverImageProvider::hits() const {
  QMutexLocker locked(&m_lock);
  return m_hits;
}

int CoverImageProvider::misses() const {
  QMutexLocker locked(&m_lock);
  return m_misses;
}
