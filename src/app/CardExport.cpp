#include "app/CardExport.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

CardExport::CardExport(QObject* parent) : QObject(parent) {}

void CardExport::reportExport(bool written) {
  emit exportReported(written);
}

QString CardExport::folder() const {
  const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  const QString base = pictures.isEmpty()
                           ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                           : pictures;
  return base.isEmpty() ? QString() : base + QStringLiteral("/Omakade");
}

QString CardExport::pathFor(const QString& label) const {
  const QString destination = folder();
  if (destination.isEmpty())
    return QString();
  QDir().mkpath(destination);
  // A period label has to become a file name: "All time" does not survive as one, and neither
  // does a label with a slash in it.
  QString slug = label.simplified();
  slug.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), QStringLiteral("-"));
  slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
  if (slug.isEmpty())
    slug = QStringLiteral("review");
  return destination + QStringLiteral("/omakade-") + slug.toLower()
         + QStringLiteral("-in-review.png");
}

bool CardExport::reveal(const QString& path) const {
  const QString target = path.isEmpty() ? folder() : path;
  if (target.isEmpty())
    return false;
  const QFileInfo info(target);
  const QString folderPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
  if (folderPath.isEmpty())
    return false;
  return QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
}
