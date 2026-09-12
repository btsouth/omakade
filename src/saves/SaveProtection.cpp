#include "saves/SaveProtection.h"
#include "launch/GameLauncher.h"
#include "library/UnifiedGameModel.h"
#include "saves/SaveBackups.h"
#include <QSet>
#include <QTimer>
#include <memory>

SaveProtection::SaveProtection(UnifiedGameModel* games, GameLauncher* launcher,
                               SaveBackups* backups, QObject* parent)
    : QObject(parent), m_games(games), m_launcher(launcher), m_backups(backups) {}
void SaveProtection::refresh() {
  if (m_busy)
    return;
  m_busy = true;
  m_cleanup.clear();
  emit changed();
  auto pending = std::make_shared<QVariantList>();
  QSet<QString> seen;
  for (int row = 0; row < m_games->rowCount(); ++row)
    for (const auto& value : m_games->installations(row)) {
      const auto game = value.toMap();
      const auto key = GameLauncher::setupKey(game);
      if (game.value("isPortal").toBool() || seen.contains(key))
        continue;
      seen.insert(key);
      pending->append(game);
    }
  auto rows = std::make_shared<QVariantList>();
  auto used = std::make_shared<QSet<QString>>();
  auto bytes = std::make_shared<qint64>(0);
  auto index = std::make_shared<int>(0);
  auto* timer = new QTimer(this);
  timer->setInterval(0);
  connect(timer, &QTimer::timeout, this, [this, timer, pending, rows, used, bytes, index] {
    const int end = qMin(*index + 20, int(pending->size()));
    while (*index < end) {
      const auto game = pending->at((*index)++).toMap();
      const auto plan = m_launcher->inspect(game);
      if (!plan.value("supported").toBool())
        continue;
      const auto context = plan.value("saveContext").toMap();
      auto entry = m_backups->coverage(context);
      entry["key"] = GameLauncher::setupKey(game);
      entry["title"] = game.value("title");
      entry["source"] = game.value("source");
      entry["context"] = context;
      for (const auto& value : entry.value("versions").toList()) {
        const auto version = value.toMap();
        const auto id = version.value("storageKey", context.value("game")).toString() + ':' +
                        version.value("id").toString();
        if (!used->contains(id)) {
          used->insert(id);
          *bytes += qMax<qint64>(0, version.value("bytes").toLongLong());
        }
      }
      rows->append(entry);
    }
    if (*index >= pending->size()) {
      timer->stop();
      timer->deleteLater();
      m_entries = *rows;
      m_bytes = *bytes;
      m_busy = false;
      emit changed();
    }
  });
  timer->start();
}
bool SaveProtection::select(const QString& key) {
  for (const auto& value : m_entries) {
    const auto entry = value.toMap();
    if (entry.value("key").toString() != key)
      continue;
    const auto c = entry.value("context").toMap();
    m_backups->selectLaunch(c.value("source").toString(), c.value("game").toString(),
                            c.value("core").toString(), c.value("flatpak").toBool(),
                            c.value("id").toString(), c.value("runner").toString(),
                            c.value("target").toString());
    return true;
  }
  return false;
}
void SaveProtection::previewCleanup() {
  m_cleanup.clear();
  QSet<QString> seen;
  for (const auto& row : m_entries) {
    const auto entry = row.toMap(), context = entry.value("context").toMap();
    QHash<QString, int> counts;
    for (const auto& value : entry.value("versions").toList()) {
      const auto version = value.toMap();
      const auto storage = version.value("storageKey", context.value("game")).toString();
      const auto id = storage + ':' + version.value("id").toString();
      if (++counts[storage] <= m_backups->retention() || seen.contains(id))
        continue;
      seen.insert(id);
      auto candidate = version;
      candidate["game"] = context.value("game");
      candidate["title"] = entry.value("title");
      m_cleanup.append(candidate);
    }
  }
  emit changed();
}
bool SaveProtection::applyCleanup() {
  const auto selected = m_cleanup;
  m_cleanup.clear();
  for (const auto& value : selected) {
    const auto version = value.toMap();
    m_backups->selectGame(version.value("game").toString());
    if (!m_backups->deleteVersion(version.value("id").toString())) {
      emit changed();
      return false;
    }
  }
  refresh();
  return true;
}
