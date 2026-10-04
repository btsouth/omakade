#pragma once

#include <QVector>
#include <QJsonArray>
#include <QString>

// pidfds pin each process through STOP and CONT, including PID reuse and reparenting.
// Owned by the pipe guard process, whose stdin closes even if Omakade is SIGKILLed.
class GuidePause final {
public:
  ~GuidePause();
  bool stop(qint64 pid, qint64 start, QString* error);
  void resume();
  QJsonArray identities() const;
private:
  struct Stopped { qint64 pid; qint64 start; int fd; };
  QVector<Stopped> m_stopped;
};
