#pragma once

#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QVariantList>

// The Omakade window's view of saved highlight clips: one game's list for its details page,
// and the actions on a clip. omakade-sessiond saves them; this only reads the library
// database and the files, and moves a clip to the trash when asked.
class HighlightClips final : public QObject {
  Q_OBJECT
  // Changes whenever a list may have: a clip saved, removed, or a still image made.
  Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
  // An empty path disables it, which is what isolated test runs use.
  explicit HighlightClips(const QString& databasePath, QObject* parent = nullptr);
  ~HighlightClips() override;

  [[nodiscard]] int revision() const { return m_revision; }
  // Where still images are kept. Render fixtures point it at their own folder.
  void setThumbnailRoot(const QString& root) { m_thumbnailRoot = root; }

  // The clips of a game's installations, newest first, as maps with id, path, url, image
  // (empty until its still is ready), seconds, trigger, label and when, a readable date.
  Q_INVOKABLE QVariantList clipsFor(const QVariantList& installations);
  Q_INVOKABLE void play(const QString& path);
  Q_INVOKABLE void showInFolder(const QString& path);
  Q_INVOKABLE void openFolder();
  // Moves the video to the trash, where it can still be restored, and forgets it.
  Q_INVOKABLE bool moveToTrash(qint64 id);

public slots:
  // Another process saved or removed a clip.
  void refresh();

signals:
  void changed();

private:
  bool open();

  QString m_databasePath;
  QString m_connection;
  QSqlDatabase m_database;
  QSet<QString> m_thumbnailsPending;
  QString m_thumbnailRoot;
  int m_revision = 0;
};
