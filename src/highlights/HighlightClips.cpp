#include "highlights/HighlightClips.h"

#include "highlights/ClipLibrary.h"
#include "highlights/HighlightSettings.h"
#include "tracking/SessionDatabase.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>
#include <QUuid>

HighlightClips::HighlightClips(const QString& databasePath, QObject* parent)
    : QObject(parent), m_databasePath(databasePath) {}

HighlightClips::~HighlightClips() {
  if (m_database.isValid()) {
    m_database.close();
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
  }
}

bool HighlightClips::open() {
  if (m_databasePath.isEmpty()) return false;
  if (m_database.isValid() && m_database.isOpen()) return true;
  m_connection = QStringLiteral("omakade-clips-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  return SessionDatabase::open(m_database, m_databasePath, m_connection);
}

QVariantList HighlightClips::clipsFor(const QVariantList& installations) {
  QVariantList result;
  if (!open()) return result;
  QList<QPair<QString, QString>> keys;
  for (const QVariant& value : installations) {
    const QVariantMap installation = value.toMap();
    const QString source = installation.value(QStringLiteral("source")).toString();
    // Sessions name a Steam game (and a native port) by its id and anything else by its path.
    for (const QString& field : {QStringLiteral("appId"), QStringLiteral("installPath"), QStringLiteral("launchTarget")}) {
      const QString key = installation.value(field).toString();
      if (!key.isEmpty() && !keys.contains(qMakePair(source, key))) keys.append({source, key});
    }
  }
  const QLocale locale;
  for (const ClipLibrary::Clip& clip : ClipLibrary::forGames(m_database, keys)) {
    const QString image = ClipLibrary::thumbnailPath(clip.path, m_thumbnailRoot);
    const bool ready = QFileInfo(image).size() > 0;
    if (!ready && !m_thumbnailsPending.contains(clip.path)) {
      m_thumbnailsPending.insert(clip.path);
      ClipLibrary::makeThumbnail(clip.path, this, [this, path = clip.path](const QString&) {
        m_thumbnailsPending.remove(path);
        ++m_revision;
        emit changed();
      }, m_thumbnailRoot);
    }
    const QDateTime when = QDateTime::fromSecsSinceEpoch(clip.createdAt);
    result.append(QVariantMap{
        {QStringLiteral("id"), clip.id},
        {QStringLiteral("path"), clip.path},
        {QStringLiteral("url"), QUrl::fromLocalFile(clip.path).toString()},
        {QStringLiteral("image"), ready ? QUrl::fromLocalFile(image).toString() : QString()},
        {QStringLiteral("seconds"), clip.seconds},
        {QStringLiteral("trigger"), clip.trigger},
        {QStringLiteral("label"), clip.label},
        {QStringLiteral("when"), locale.toString(when.date(), QLocale::ShortFormat) + QStringLiteral(" · ") +
                                     locale.toString(when.time(), QLocale::ShortFormat)},
    });
  }
  return result;
}

void HighlightClips::play(const QString& path) {
  if (QFileInfo(path).isFile()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void HighlightClips::showInFolder(const QString& path) {
  const QFileInfo info(path);
  if (info.dir().exists()) QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void HighlightClips::openFolder() {
  const QString folder = HighlightSettings::folder();
  QDir().mkpath(folder);
  QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

bool HighlightClips::moveToTrash(qint64 id) {
  if (!open()) return false;
  const ClipLibrary::Clip clip = ClipLibrary::byId(m_database, id);
  if (clip.id == 0) return false;
  // Named from the file's path and time, so it has to be found before the file moves.
  const QString image = ClipLibrary::thumbnailPath(clip.path, m_thumbnailRoot);
  // A file already gone is forgotten as well; one that will not move stays listed.
  if (QFileInfo::exists(clip.path) && !QFile::moveToTrash(clip.path)) return false;
  QFile::remove(image);
  const bool forgotten = ClipLibrary::forget(m_database, id);
  ++m_revision;
  emit changed();
  return forgotten;
}

void HighlightClips::refresh() {
  ++m_revision;
  emit changed();
}
