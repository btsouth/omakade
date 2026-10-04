#include "guide/GuideActions.h"
#include "tracking/ProcFs.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <csignal>
#include <sys/syscall.h>
#include <unistd.h>

namespace GuideActions {
QString key(const QVariantMap& game) {
  if (game.isEmpty()) return {};
  const auto identity = game.value("source").toString().toUtf8() + '\0' + game.value("path", game.value("appId")).toString().toUtf8();
  return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
QString notes(const QString& directory, const QString& key) {
  if (key.isEmpty()) return {};
  QFile file(directory + '/' + key + ".txt");
  return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(8192)) : QString{};
}
bool saveNotes(const QString& directory, const QString& key, const QString& text) {
  if (key.isEmpty() || text.toUtf8().size() > 8192 || !QDir().mkpath(directory)) return false;
  QSaveFile file(directory + '/' + key + ".txt");
  const auto bytes = text.toUtf8();
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QString mangoConfig(const QString& socket, const QString& level, int limit) {
  QString config = "no_display\ncontrol=" + socket + "\nfps_limit=" + QString::number(qMax(0, limit)) + '\n';
  if (level == "full") config += "full\n";
  else {
    config += "fps\ncpu_stats=0\ngpu_stats=0\n";
    config += level == "frametime" ? "frame_timing=1\n" : "frame_timing=0\n";
  }
  return config;
}
QProcessEnvironment mangoEnvironment(QProcessEnvironment base, bool installed, const QString& config) {
  if (installed && !config.isEmpty()) {
    base.insert("MANGOHUD", "1");
    base.insert("MANGOHUD_CONFIGFILE", config);
    base.insert("MANGOHUD_CONFIG", "read_cfg,no_display");
    base.remove("MANGOHUD_FPS_LIMIT");
  }
  return base;
}
QByteArray mangoVisibilityCommand(bool before, bool after) { return before == after ? QByteArray{} : QByteArray(":hud;"); }
Tree::~Tree() { clear(); }
void Tree::clear() { for (const auto& e : m_entries) ::close(e.fd); m_entries.clear(); }
bool Tree::adopt(const QJsonArray& identities) {
  clear();
  for (const auto& value : identities) {
    const auto e = value.toObject();
    const auto pid = e.value("pid").toInteger(), start = e.value("start").toInteger();
    if (pid <= 1 || pid == ::getpid() || start <= 0) { clear(); return false; }
    const int fd = ::syscall(SYS_pidfd_open, pid, 0);
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (fd < 0 || QFileInfo(stat).ownerId() != ::getuid() || !ProcFs::processAlive(pid, start)) {
      if (fd >= 0) ::close(fd);
      clear(); return false;
    }
    m_entries.push_back({pid, start, fd});
  }
  return !m_entries.empty();
}
bool Tree::pin(qint64 pid, qint64 start) {
  if (!ProcFs::processAlive(pid, start)) return false;
  QJsonArray ids{QJsonObject{{"pid", pid}, {"start", start}}};
  QSet<qint64> selected{pid};
  const auto processes = ProcFs::listProcesses();
  for (int pass = 0; pass < 32; ++pass) {
    bool added = false;
    for (const auto& process : processes) {
      QFile stat(QStringLiteral("/proc/%1/stat").arg(process.pid));
      if (!stat.open(QIODevice::ReadOnly)) continue;
      const auto bytes = stat.readAll();
      const auto fields = bytes.mid(bytes.lastIndexOf(')') + 2).simplified().split(' ');
      if (fields.size() < 2 || selected.contains(process.pid) || !selected.contains(fields[1].toLongLong())) continue;
      selected.insert(process.pid); added = true;
      ids.append(QJsonObject{{"pid", process.pid}, {"start", process.procStart}});
    }
    if (!added) break;
  }
  return adopt(ids);
}
QJsonArray Tree::identities() const {
  QJsonArray ids;
  for (const auto& e : m_entries) ids.append(QJsonObject{{"pid", e.pid}, {"start", e.start}});
  return ids;
}
void Tree::signal(int number) {
  for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) ::syscall(SYS_pidfd_send_signal, it->fd, number, nullptr, 0);
}
bool Tree::alive() const {
  for (const auto& e : m_entries) if (::syscall(SYS_pidfd_send_signal, e.fd, 0, nullptr, 0) == 0) {
    QFile stat(QStringLiteral("/proc/%1/stat").arg(e.pid));
    if (stat.open(QIODevice::ReadOnly)) { const auto bytes = stat.readAll(); if (bytes.mid(bytes.lastIndexOf(')') + 2, 1) != "Z") return true; }
  }
  return false;
}
}
