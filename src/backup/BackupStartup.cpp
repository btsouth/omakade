#include "backup/BackupStartup.h"
#include <QProcess>
#include <QtConcurrentRun>

namespace {
QString stopRecorder() {
  QProcess systemctl;
  systemctl.setProcessChannelMode(QProcess::MergedChannels);
  systemctl.start("systemctl", {"--user", "stop", "omakade-sessiond.service"});
  if (!systemctl.waitForFinished(30000)) {
    systemctl.kill();
    systemctl.waitForFinished(5000);
    return "Could not stop omakade-sessiond.service. Stop the recorder, then retry.";
  }
  if (systemctl.exitStatus() != QProcess::NormalExit || systemctl.exitCode() != 0)
    return "Could not stop omakade-sessiond.service: " +
           QString::fromUtf8(systemctl.readAll()).trimmed();
  return {};
}
} // namespace

BackupStartup::BackupStartup(BackupRecovery::Paths paths, QObject* parent)
    : QObject(parent), m_paths(std::move(paths)) {
  connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
    const auto result = m_watcher.result();
    m_busy = false;
    m_canUndo = result.canUndo;
    m_recorderRunning = result.recorderRunning;
    m_message = result.message;
    emit changed();
    if (result.okay)
      emit resolved();
  });
}
BackupStartup::~BackupStartup() { m_watcher.waitForFinished(); }
void BackupStartup::retry() { run(false); }
void BackupStartup::undo() {
  if (m_canUndo)
    run(true);
}
void BackupStartup::stopRecorderAndRetry() {
  if (m_recorderRunning)
    run(false, true);
}
void BackupStartup::run(bool undo, bool stop) {
  if (m_busy)
    return;
  m_busy = true;
  m_canUndo = false;
  m_message = stop   ? "Stopping the play-session recorder…"
              : undo ? "Restoring your previous library…"
                     : "Applying your backup…";
  emit changed();
  const auto paths = m_paths;
  m_watcher.setFuture(QtConcurrent::run([paths, undo, stop] {
    BackupRecovery recovery(paths);
    Result result;
    if (stop)
      result.message = stopRecorder();
    if (result.message.isEmpty())
      result.okay = undo ? recovery.undo(&result.message) : recovery.resume(&result.message);
    result.recorderRunning = !result.okay && result.message == BackupDatabase::RecorderRunning;
    if (result.okay)
      result.message = "Your library is ready.";
    else {
      const auto phase = recovery.status();
      result.canUndo = phase == "queued" || phase == "prepared" || phase == "undoing";
    }
    return result;
  }));
}
