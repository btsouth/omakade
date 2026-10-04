#pragma once
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QString>
#include <QVariantMap>
#include <vector>

namespace GuideActions {
QString key(const QVariantMap& game);
QString notes(const QString& directory, const QString& key);
bool saveNotes(const QString& directory, const QString& key, const QString& text);
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
  void signal(int number);
  bool alive() const;
  void clear();
private:
  struct Entry { qint64 pid, start; int fd; };
  std::vector<Entry> m_entries;
};
}
