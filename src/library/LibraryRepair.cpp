#include "library/LibraryRepair.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "metadata/GameMetadata.h"
#include <QTimer>

LibraryRepair::LibraryRepair(UnifiedGameModel* games, GameMetadata* metadata, const QString& path,
                             QObject* parent)
    : QObject(parent), m_games(games), m_metadata(metadata), m_state(path, QSettings::IniFormat) {
  m_key = m_state.value("current").toString();
  m_source = m_state.value("source").toString();
  m_reason = m_state.value("reason").toString();
  auto* timer = new QTimer(this);
  timer->setSingleShot(true);
  timer->setInterval(150);
  connect(timer, &QTimer::timeout, this, [this] {
    if (m_active)
      refresh();
  });
  connect(games, &QAbstractItemModel::dataChanged, timer, [this, timer] {
    if (m_active)
      timer->start();
  });
  connect(games, &QAbstractItemModel::modelReset, timer, [this, timer] {
    if (m_active)
      timer->start();
  });
  connect(games, &QAbstractItemModel::rowsInserted, timer, [this, timer] {
    if (m_active)
      timer->start();
  });
  connect(games, &QAbstractItemModel::rowsRemoved, timer, [this, timer] {
    if (m_active)
      timer->start();
  });
}
void LibraryRepair::save() {
  m_state.setValue("current", m_key);
  m_state.setValue("source", m_source);
  m_state.setValue("reason", m_reason);
  m_state.sync();
  if (m_state.status() != QSettings::NoError)
    m_message = "Could not save review position. Check local storage.";
}
QVariantMap LibraryRepair::current() const {
  auto result = m_current;
  result["selected"] = m_selected.contains(m_key);
  result["undoIdentity"] = m_games->hasRepairCheckpoint(m_key, "identity");
  result["undoArtwork"] = m_games->hasRepairCheckpoint(m_key, "artwork");
  return result;
}
void LibraryRepair::setSource(const QString& value) {
  m_selected.clear();
  m_source = value;
  m_key.clear();
  refresh();
}
void LibraryRepair::setReason(const QString& value) {
  if (!QStringList{"", "identification", "artwork", "unavailable", "duplicates"}.contains(value))
    return;
  m_selected.clear();
  m_reason = value;
  m_key.clear();
  refresh();
}
void LibraryRepair::refresh() {
  m_active = true;
  m_entries.clear();
  m_sources.clear();
  m_current.clear();
  for (int row = 0; row < m_games->rowCount(); ++row) {
    auto game = m_games->reviewGame(row);
    if (game.value("isPortal").toBool())
      continue;
    const auto source = game.value("source").toString();
    if (!m_sources.contains(source))
      m_sources.append(source);
    const auto reasons = m_games->reviewReasons(row);
    game["reasons"] = reasons;
    if (m_metadata) {
      const auto identity = m_metadata->entry(game.value("metadataKey").toString());
      game["matchedTitle"] = identity.value("title");
      game["igdbId"] = identity.value("igdbId");
    }
    if (game.value("metadataKey").toString() == m_key)
      m_current = game;
    if (reasons.isEmpty() || (!m_source.isEmpty() && source != m_source) ||
        (!m_reason.isEmpty() && !reasons.contains(m_reason)))
      continue;
    m_entries.append(game);
  }
  QSet<QString> remaining;
  for (const auto& entry : m_entries)
    remaining.insert(entry.toMap().value("metadataKey").toString());
  m_selected.intersect(remaining);
  m_sources.sort();
  if (m_current.isEmpty() && !m_entries.isEmpty()) {
    m_current = m_entries.first().toMap();
    m_key = m_current.value("metadataKey").toString();
  }
  save();
  emit changed();
}
void LibraryRepair::move(int delta) {
  delta = delta < 0 ? -1 : 1;
  if (m_entries.isEmpty()) {
    m_key.clear();
    m_current.clear();
    save();
    emit changed();
    return;
  }
  int at = -1;
  for (int i = 0; i < m_entries.size(); ++i)
    if (m_entries[i].toMap().value("metadataKey").toString() == m_key) {
      at = i;
      break;
    }
  at = at < 0 ? (delta < 0 ? m_entries.size() - 1 : 0)
              : (at + delta + m_entries.size()) % m_entries.size();
  m_current = m_entries[at].toMap();
  m_key = m_current.value("metadataKey").toString();
  save();
  emit changed();
}
bool LibraryRepair::checkpoint(const QString& kind) {
  const bool ok = m_games->repairCheckpoint(m_key, kind, false);
  m_message = ok ? "Recovery point saved. Identity and artwork can be undone separately."
                 : "Could not save a recovery point. Stop metadata work and wait for it to finish, "
                   "then retry. Check local storage if this continues.";
  emit changed();
  return ok;
}
bool LibraryRepair::undo(const QString& kind) {
  const bool ok = m_games->repairCheckpoint(m_key, kind, true);
  m_message = ok ? "Correction undone. Other choices were kept."
                 : "Could not undo. Stop metadata work and check local storage, then retry.";
  refresh();
  return ok;
}
void LibraryRepair::retry(bool all) {
  if (!m_metadata || !m_metadata->reviewWritable()) {
    m_message = "Wait for current metadata work to finish or stop it first.";
    emit changed();
    return;
  }
  m_metadata->retryReviewGames(all ? m_entries.mid(0, 100) : QVariantList{m_current});
  m_message =
      all ? "Retrying up to 100 games in these filters. Confirm ambiguous matches individually."
          : "Retry requested.";
  emit changed();
}

QStringList LibraryRepair::selectedTitles() const {
  QStringList titles;
  for (const auto& item : m_entries) {
    const auto game = item.toMap();
    if (m_selected.contains(game.value("metadataKey").toString()))
      titles << game.value("title").toString();
  }
  return titles;
}
void LibraryRepair::toggleSelected() {
  bool present = false;
  for (const auto& item : m_entries)
    if (item.toMap().value("metadataKey").toString() == m_key) {
      present = true;
      break;
    }
  if (!present)
    return;
  if (m_selected.contains(m_key))
    m_selected.remove(m_key);
  else if (!m_key.isEmpty() && m_selected.size() < 100)
    m_selected.insert(m_key);
  else
    m_message = "Select up to 100 games for one retry batch.";
  emit changed();
}
void LibraryRepair::retrySelected() {
  if (!m_metadata || !m_metadata->reviewWritable())
    return;
  QVariantList selected;
  for (const auto& item : m_entries)
    if (m_selected.contains(item.toMap().value("metadataKey").toString()))
      selected.append(item);
  if (selected.isEmpty())
    return;
  m_metadata->retryReviewGames(selected);
  m_message = "Retry requested for the selected games. Completed corrections remain if you stop.";
  emit changed();
}
QStringList LibraryRepair::reasonsFor(const QString& key) const {
  for (int row = 0; row < m_games->rowCount(); ++row)
    if (m_games->index(row).data(GameRoles::MetadataKey).toString() == key)
      return m_games->reviewReasons(row);
  return {};
}
