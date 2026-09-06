#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

// Decodes a game's cover once and keeps it for as long as it is worth keeping.
//
// Qt keeps a small cache of images no view is currently showing, a couple of megabytes, which
// is roughly twenty covers at the size a card asks for. That is less than a single screenful,
// so a card that scrolled out of view and back, or survived a filter change, had to read and
// decode its artwork from disk again and showed a placeholder until it finished.
//
// The cache is deliberately bounded rather than sized to the library. Holding every cover of a
// large library would grow with both the number of games and the cover size setting, so the
// libraries that need it most would be the ones that blew the budget, and it would be spent on
// artwork the person is nowhere near. A few hundred covers is several screens in either
// direction, which is the distance people actually scroll and come back from. Past that the
// artwork is decoded again, which costs a few milliseconds for a thumbnail and was never the
// problem: the problem was a limit far below one screen.
//
// Registered as "covers": a card asks for image://covers/f<file path> or image://covers/q<path>
// for bundled console art.
class CoverImageProvider final : public QQuickImageProvider {
public:
  // 128 MB holds several screens of desktop cards comfortably, and still several at the largest
  // cover size on a 4K television, where a single card can decode to about 600 KB.
  explicit CoverImageProvider(int limitMegabytes = 128);

  QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

  // Counts since startup, for diagnostics and tests.
  [[nodiscard]] int hits() const;
  [[nodiscard]] int misses() const;

private:
  mutable QMutex m_lock;
  QCache<QString, QImage> m_cache;
  int m_hits = 0;
  int m_misses = 0;
};
