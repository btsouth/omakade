#pragma once

#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

// One gpu-screen-recorder replay buffer and its IPC socket. The recorder keeps the last
// few minutes of a monitor in memory and writes a file only when asked, so a running
// buffer costs the game an encoder session and nothing else.
//
// The recorder is a child of this process and is told to exit if the process dies, so a
// crashed daemon never leaves a recorder running. It is started by its full path, which
// keeps it out of Omarchy's own `pgrep -f "^gpu-screen-recorder"` start and stop.
class ReplayBuffer final : public QObject {
  Q_OBJECT

public:
  struct Options {
    QString program;
    // Hyprland's name for the output, which gpu-screen-recorder captures by the same name.
    QString monitor;
    // Where the recorder writes saved replays before they are filed.
    QString directory;
    QString socketPath;
    int bufferSeconds = 120;
  };

  enum class State { Stopped, Starting, Running, Failed };
  Q_ENUM(State)

  // path is empty when the save failed, and error then says why.
  using SaveDone = std::function<void(const QString& path, const QString& error)>;

  explicit ReplayBuffer(QObject* parent = nullptr);
  ~ReplayBuffer() override;

  [[nodiscard]] static QStringList arguments(const Options& options);
  // The installed recorder's full path, or empty.
  [[nodiscard]] static QString findRecorder();
  // A fresh socket path under the runtime directory. Every buffer gets its own, so a
  // stopping recorder removing its socket can never take a newer one's with it.
  [[nodiscard]] static QString newSocketPath();
  // Ends recorders an earlier daemon left behind, found by their socket path prefix.
  // Returns how many were asked to stop.
  static int reapOrphans();

  void start(const Options& options);
  void stop();
  // Saves the last `seconds` of the buffer. Saves run one at a time, in order.
  void save(int seconds, SaveDone done);

  [[nodiscard]] State state() const { return m_state; }
  [[nodiscard]] const Options& options() const { return m_options; }
  [[nodiscard]] QString lastError() const { return m_error; }

  // How long to wait for the recorder's socket, and for a save to be written.
  int startTimeoutMs = 10000;
  int saveTimeoutMs = 30000;

signals:
  void stateChanged(ReplayBuffer::State state);

private:
  struct PendingSave {
    int seconds = 0;
    SaveDone done;
  };

  void setState(State state, const QString& error = {});
  void checkStarted();
  void runNextSave();
  void finishSaves(const QString& error);
  [[nodiscard]] QString outputTail() const;

  QProcess* m_process = nullptr;
  Options m_options;
  State m_state = State::Stopped;
  QString m_error;
  QByteArray m_output;
  QTimer m_startPoll;
  qint64 m_startDeadline = 0;
  QQueue<PendingSave> m_saves;
  bool m_saving = false;
  QLocalSocket* m_saveSocket = nullptr;
  int m_nextId = 1;
};
