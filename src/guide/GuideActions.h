#pragma once
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QString>
#include <vector>

namespace GuideActions {
QString mangoConfig(const QString& socket, const QString& level, int limit);
QProcessEnvironment mangoEnvironment(QProcessEnvironment base, bool installed,
                                     const QString& config);
QByteArray mangoVisibilityCommand(bool before, bool after);
// Pins an exact process tree for graceful termination and later explicit escalation.
class Tree final {
public:
  ~Tree();
  bool pin(qint64 pid, qint64 start);
  bool adopt(const QJsonArray& identities);
  QJsonArray identities() const;
  void signal(int number, qint64 exceptPid = -1);
  bool alive() const;
  void clear();
private:
  struct Entry { qint64 pid, start; int fd; };
  std::vector<Entry> m_entries;
};
}
