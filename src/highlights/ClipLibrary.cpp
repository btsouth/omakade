#include "highlights/ClipLibrary.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>

#include <cstdio>

namespace ClipLibrary {
namespace {

constexpr int kThumbnailTimeoutMs = 15000;

Clip read(const QSqlQuery& query) {
  Clip clip;
  clip.id = query.value(0).toLongLong();
  clip.path = query.value(1).toString();
  clip.source = query.value(2).toString();
  clip.gameKey = query.value(3).toString();
  clip.title = query.value(4).toString();
  clip.createdAt = query.value(5).toLongLong();
  clip.seconds = query.value(6).toInt();
  clip.trigger = query.value(7).toString();
  clip.label = query.value(8).toString();
  return clip;
}

const QString kColumns = QStringLiteral(
    "id, path, source, game_key, title, created_at, seconds, trigger, label");

QVector<Clip> existing(QSqlQuery& query) {
  QVector<Clip> clips;
  while (query.next()) {
    Clip clip = read(query);
    if (QFileInfo(clip.path).isFile()) clips.append(clip);
  }
  return clips;
}

} // namespace

bool ensureSchema(QSqlDatabase& database) {
  QSqlQuery query(database);
  return query.exec(QStringLiteral(
             "CREATE TABLE IF NOT EXISTS highlight_clips (id INTEGER PRIMARY KEY, path TEXT NOT NULL "
             "UNIQUE, source TEXT NOT NULL DEFAULT '', game_key TEXT NOT NULL, title TEXT NOT NULL "
             "DEFAULT '', created_at INTEGER NOT NULL, seconds INTEGER NOT NULL DEFAULT 0, trigger "
             "TEXT NOT NULL DEFAULT 'button', label TEXT NOT NULL DEFAULT '')")) &&
         query.exec(QStringLiteral(
             "CREATE INDEX IF NOT EXISTS highlight_clips_game ON highlight_clips(source, game_key)"));
}

QString folderName(const QString& title) {
  static const QRegularExpression refused(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1f]"));
  static const QRegularExpression spaces(QStringLiteral("\\s+"));
  QString name = title;
  name.replace(refused, QStringLiteral(" "));
  name = name.replace(spaces, QStringLiteral(" ")).trimmed();
  while (name.endsWith(QLatin1Char('.'))) name.chop(1);
  name = name.trimmed().left(120).trimmed();
  if (name.isEmpty() || name.startsWith(QLatin1Char('.'))) return QStringLiteral("Game");
  return name;
}

Clip file(QSqlDatabase& database, const QString& recordedPath, const QString& root, Clip clip,
          QString* error) {
  const auto fail = [&](const QString& message) {
    if (error != nullptr) *error = message;
    Clip none;
    return none;
  };
  const QFileInfo recorded(recordedPath);
  if (!recorded.isFile() || recorded.size() <= 0) return fail(QStringLiteral("The recorder did not write a video"));
  const QString name = folderName(clip.title);
  const QDir folder(root + QLatin1Char('/') + name);
  if (!QDir().mkpath(folder.absolutePath())) return fail(QStringLiteral("The clip folder could not be created"));
  if (clip.createdAt <= 0) clip.createdAt = QDateTime::currentSecsSinceEpoch();
  const QString stamp = QDateTime::fromSecsSinceEpoch(clip.createdAt).toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));
  const QString suffix = recorded.suffix().isEmpty() ? QStringLiteral("mp4") : recorded.suffix().toLower();
  QString target = folder.filePath(QStringLiteral("%1 %2.%3").arg(name, stamp, suffix));
  for (int copy = 2; QFileInfo::exists(target); ++copy)
    target = folder.filePath(QStringLiteral("%1 %2 (%3).%4").arg(name, stamp).arg(copy).arg(suffix));
  if (!QFile::rename(recordedPath, target)) return fail(QStringLiteral("The clip could not be moved into its folder"));

  QSqlQuery query(database);
  if (database.isOpen())
    query.prepare(QStringLiteral("INSERT INTO highlight_clips (path, source, game_key, title, created_at, "
                                 "seconds, trigger, label) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
  // A null QString binds as SQL NULL, which the NOT NULL columns refuse.
  const auto text = [](const QString& value) { return value.isNull() ? QStringLiteral("") : value; };
  query.addBindValue(target);
  query.addBindValue(text(clip.source));
  query.addBindValue(text(clip.gameKey));
  query.addBindValue(text(clip.title));
  query.addBindValue(clip.createdAt);
  query.addBindValue(clip.seconds);
  query.addBindValue(clip.trigger.isEmpty() ? QStringLiteral("button") : clip.trigger);
  query.addBindValue(text(clip.label));
  if (!database.isOpen() || !query.exec()) {
    // The video is kept in its folder either way: losing the clip would be worse than
    // Omakade not listing it.
    if (error != nullptr)
      *error = database.isOpen() ? QStringLiteral("The clip could not be recorded: ") + query.lastError().text()
                                 : QStringLiteral("The library database is unavailable");
    clip.id = 0;
    clip.path = target;
    return clip;
  }
  clip.id = query.lastInsertId().toLongLong();
  clip.path = target;
  if (clip.trigger.isEmpty()) clip.trigger = QStringLiteral("button");
  return clip;
}

QVector<Clip> forGame(QSqlDatabase& database, const QString& source, const QString& gameKey) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral("SELECT %1 FROM highlight_clips WHERE source = ? AND game_key = ? "
                               "ORDER BY created_at DESC, id DESC").arg(kColumns));
  query.addBindValue(source);
  query.addBindValue(gameKey);
  if (!query.exec()) return {};
  return existing(query);
}

QVector<Clip> recent(QSqlDatabase& database, int limit) {
  QSqlQuery query(database);
  // Read past missing files so a few deleted clips do not shorten the list.
  query.prepare(QStringLiteral("SELECT %1 FROM highlight_clips ORDER BY created_at DESC, id DESC LIMIT ?").arg(kColumns));
  query.addBindValue(qMax(0, limit) * 4 + 16);
  if (!query.exec()) return {};
  QVector<Clip> clips = existing(query);
  if (clips.size() > limit) clips.resize(qMax(0, limit));
  return clips;
}

Clip byId(QSqlDatabase& database, qint64 id) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral("SELECT %1 FROM highlight_clips WHERE id = ?").arg(kColumns));
  query.addBindValue(id);
  if (!query.exec() || !query.next()) return {};
  return read(query);
}

bool forget(QSqlDatabase& database, qint64 id) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral("DELETE FROM highlight_clips WHERE id = ?"));
  query.addBindValue(id);
  return query.exec();
}

namespace {
// Runs a tool to completion without blocking: done gets its standard output, or nothing
// when it could not start, failed or ran past the timeout.
void run(const QString& program, const QStringList& arguments, QObject* context,
         std::function<void(bool, const QByteArray&)> done) {
  auto* process = new QProcess(context);
  auto finished = std::make_shared<bool>(false);
  const auto complete = [process, finished, done](bool ok) {
    if (*finished) return;
    *finished = true;
    process->deleteLater();
    done(ok, ok ? process->readAllStandardOutput() : QByteArray());
  };
  QObject::connect(process, &QProcess::finished, context, [complete](int code, QProcess::ExitStatus status) {
    complete(code == 0 && status == QProcess::NormalExit);
  });
  QObject::connect(process, &QProcess::errorOccurred, context, [complete](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) complete(false);
  });
  QTimer::singleShot(kThumbnailTimeoutMs, process, [process] { process->kill(); });
  process->start(program, arguments);
}
} // namespace

void trimEnd(const QString& path, double seconds, QObject* context, std::function<void(bool)> done) {
  const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (seconds <= 0 || ffprobe.isEmpty() || ffmpeg.isEmpty() || !QFileInfo(path).isFile()) {
    if (done) done(false);
    return;
  }
  run(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                QStringLiteral("format=duration"), QStringLiteral("-of"), QStringLiteral("csv=p=0"), path},
      context, [path, seconds, ffmpeg, context, done](bool ok, const QByteArray& output) {
        bool parsed = false;
        const double duration = output.trimmed().toDouble(&parsed);
        const double keep = duration - seconds;
        if (!ok || !parsed || keep < 1.0) {
          if (done) done(false);
          return;
        }
        const QFileInfo info(path);
        const QString partial = info.absolutePath() + QStringLiteral("/.") + info.completeBaseName() +
                                QStringLiteral(".trim.") + info.suffix();
        run(ffmpeg, {QStringLiteral("-nostdin"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-i"), path, QStringLiteral("-t"), QString::number(keep, 'f', 3),
                     QStringLiteral("-map"), QStringLiteral("0"), QStringLiteral("-c"), QStringLiteral("copy"),
                     QStringLiteral("-movflags"), QStringLiteral("+faststart"), QStringLiteral("-y"), partial},
            context, [path, partial, done](bool cut, const QByteArray&) {
              // rename(2) replaces the original in one step, so it is never missing.
              const bool replaced = cut && QFileInfo(partial).size() > 0 &&
                                    ::rename(QFile::encodeName(partial).constData(), QFile::encodeName(path).constData()) == 0;
              QFile::remove(partial);
              if (done) done(replaced);
            });
      });
}

QString thumbnailPath(const QString& clipPath) {
  const QFileInfo info(clipPath);
  const QByteArray key = (info.absoluteFilePath() + QLatin1Char(':') +
                          QString::number(info.lastModified().toMSecsSinceEpoch())).toUtf8();
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/clip-thumbnails/") +
         QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()) +
         QStringLiteral(".jpg");
}

void makeThumbnail(const QString& clipPath, QObject* context, std::function<void(const QString&)> done) {
  const QString image = thumbnailPath(clipPath);
  if (QFileInfo(image).size() > 0) { if (done) done(image); return; }
  const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty() || !QFileInfo(clipPath).isFile() || !QDir().mkpath(QFileInfo(image).absolutePath())) {
    if (done) done({});
    return;
  }
  // Written beside the final name and renamed, so a reader never sees half an image.
  const QString partial = image + QStringLiteral(".part.jpg");
  auto* process = new QProcess(context);
  auto finished = std::make_shared<bool>(false);
  const auto complete = [process, finished, image, partial, done](bool ok) {
    if (*finished) return;
    *finished = true;
    process->deleteLater();
    const bool written = ok && QFileInfo(partial).size() > 0 &&
                         ::rename(QFile::encodeName(partial).constData(), QFile::encodeName(image).constData()) == 0;
    QFile::remove(partial);
    if (done) done(written ? image : QString());
  };
  QObject::connect(process, &QProcess::finished, context, [complete](int code, QProcess::ExitStatus status) {
    complete(code == 0 && status == QProcess::NormalExit);
  });
  QObject::connect(process, &QProcess::errorOccurred, context, [complete](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) complete(false);
  });
  QTimer::singleShot(kThumbnailTimeoutMs, process, [process] { process->kill(); });
  // Near the end, where the moment a clip was saved for usually is. thumbnail picks the most
  // typical of the next frames, which skips a black or blurred one.
  process->start(ffmpeg, {QStringLiteral("-nostdin"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                          QStringLiteral("-sseof"), QStringLiteral("-4"), QStringLiteral("-i"), clipPath,
                          QStringLiteral("-vf"), QStringLiteral("thumbnail=30,scale=640:-2"),
                          QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-q:v"), QStringLiteral("4"),
                          QStringLiteral("-y"), partial});
}

} // namespace ClipLibrary
