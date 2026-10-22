#pragma once

#include "highlights/ClipLibrary.h"
#include "highlights/HighlightSettings.h"
#include "highlights/ReplayBuffer.h"
#include "tracking/SessionTitleIndex.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <functional>

// Keeps a replay buffer running on the game's monitor while highlights are on, and turns
// clip requests into filed clips. It lives on omakade-sessiond's guide thread and follows
// the same game the in-game guide does: the guide's snapshot names the game, its title and
// its output.
//
// A game the snapshot stops naming keeps its buffer while its process lives, because a
// Steam game behind another window drops out of the snapshot without having ended.
class HighlightService final : public QObject {
  Q_OBJECT

public:
  // Everything outside this class it touches, so tests can stand in for each.
  struct Environment {
    QString configPath;
    QString root;
    QString recorder;
    std::function<bool(qint64 pid, qint64 procStart)> alive;
    std::function<void(const QString& title, const QString& body, const QString& image)> notify;
    // Opens the library database connection clips are recorded through.
    std::function<bool(QSqlDatabase&)> openDatabase;
    std::function<qint64()> now = [] { return QDateTime::currentSecsSinceEpoch(); };
  };

  struct Game {
    qint64 pid = 0;
    qint64 procStart = -1;
    QString source;
    QString key;
    // The library's title, when the snapshot had its metadata.
    QString title;
    // What the session or window called the game, for when the library has nothing.
    QString fallbackTitle;
    QString monitor;
    [[nodiscard]] bool sameProcess(const Game& other) const {
      return pid == other.pid && procStart == other.procStart;
    }
  };

  explicit HighlightService(Environment environment, QObject* parent = nullptr);
  ~HighlightService() override;

  [[nodiscard]] static Environment defaultEnvironment();
  // The game a guide snapshot describes, or one with pid 0 when it names none.
  [[nodiscard]] static Game gameFrom(const QVariantMap& session, const QVariantMap& metadata, const QString& output);

  // A new snapshot. Starts, moves or stops the buffer as needed.
  void follow(const Game& game);
  // Saves the last clip length of the buffer. trigger is "button" or "achievement"; label
  // names what the clip shows. trimEndMs leaves out the newest moments, such as the time the
  // guide card has been on screen. done, when given, gets the stored clip or an error.
  void clip(const QString& trigger, const QString& label = {},
            std::function<void(const ClipLibrary::Clip&, const QString&)> done = {}, qint64 trimEndMs = 0);

  // Why a clip cannot be saved right now, or empty when it can.
  [[nodiscard]] QString unavailableReason() const;
  // What the guide card shows while a buffer runs for the game in view: {seconds, saving,
  // saved, failed, error}. saved and failed count up, so the card can tell a new outcome.
  [[nodiscard]] QJsonObject guideStatus() const;
  [[nodiscard]] bool running() const { return m_buffer.state() == ReplayBuffer::State::Running; }
  [[nodiscard]] const Game& game() const { return m_game; }
  // The name clips of the current game are filed under.
  [[nodiscard]] QString displayTitle() const;
  [[nodiscard]] HighlightSettings::Values settings() const { return m_settings; }
  [[nodiscard]] ReplayBuffer& buffer() { return m_buffer; }

  // Re-reads the settings and reconciles. Called on every snapshot and by its own timer, so
  // turning highlights off stops the buffer even with no game events.
  void reconcile();

signals:
  void statusChanged();
  void clipSaved(const ClipLibrary::Clip& clip);

private:
  void store(const QString& path, const ClipLibrary::Clip& clip,
             const std::function<void(const ClipLibrary::Clip&, const QString&)>& done);
  void startBuffer();
  void stopBuffer();
  void bufferStateChanged(ReplayBuffer::State state);
  [[nodiscard]] QString recordingDirectory() const;
  bool openDatabase();
  // The library's own title for a content path, which beats a disc id or a file name.
  QString libraryTitle(const QString& gamePath);

  Environment m_environment;
  HighlightSettings::Values m_settings;
  ReplayBuffer m_buffer;
  Game m_game;
  // The game the running buffer was started for.
  Game m_bufferGame;
  QTimer m_tick;
  QTimer m_retry;
  int m_failures = 0;
  int m_saving = 0;
  int m_savedCount = 0;
  int m_failedCount = 0;
  QString m_lastError;
  bool m_failureReported = false;
  int m_bufferSeconds = 0;
  QSqlDatabase m_database;
  SessionTitleIndex m_titles;
  qint64 m_titlesToken = -1;
  QString m_libraryTitle;
  QString m_connectionName;
};
