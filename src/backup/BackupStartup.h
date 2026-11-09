#pragma once
#include "backup/BackupRecovery.h"
#include <QFutureWatcher>
#include <QObject>
#include <QUrl>

// Used only while startup owns the application instance and no library models
// or account services exist. The UI must not proceed until resolved is emitted.
class BackupStartup final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
  Q_PROPERTY(bool recorderRunning READ recorderRunning NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(QUrl recoveryFolder READ recoveryFolder CONSTANT)
public:
  explicit BackupStartup(BackupRecovery::Paths paths, QObject* parent = nullptr);
  ~BackupStartup() override;
  bool busy() const { return m_busy; }
  bool canUndo() const { return m_canUndo; }
  bool recorderRunning() const { return m_recorderRunning; }
  QString message() const { return m_message; }
  QUrl recoveryFolder() const { return QUrl::fromLocalFile(m_paths.state); }
  Q_INVOKABLE void retry();
  Q_INVOKABLE void undo();
  // Stops omakade-sessiond.service, then retries. Nothing starts it again here.
  Q_INVOKABLE void stopRecorderAndRetry();
signals:
  void changed();
  void resolved();

private:
  struct Result {
    bool okay = false;
    bool canUndo = false;
    bool recorderRunning = false;
    QString message;
  };
  void run(bool undo, bool stopRecorder = false);
  BackupRecovery::Paths m_paths;
  bool m_busy = false;
  bool m_canUndo = false;
  bool m_recorderRunning = false;
  QString m_message;
  QFutureWatcher<Result> m_watcher;
};
