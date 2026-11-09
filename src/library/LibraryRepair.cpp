#include "library/LibraryRepair.h"
#include "library/ConsoleCatalog.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "launch/GameLauncher.h"
#include "metadata/GameMetadata.h"
#include "saves/SaveBackups.h"
#include <QFileInfo>
#include <QTimer>

namespace {
QString relocationStateKey(const QString& key) {
  const auto encoded = key.toUtf8().toBase64(QByteArray::Base64UrlEncoding |
                                             QByteArray::OmitTrailingEquals);
  return QStringLiteral("relocations/") + QString::fromLatin1(encoded);
}
QString canonicalContentPath(const QString& path) {
  if (QFileInfo::exists(path))
    return QFileInfo(path).canonicalFilePath();
  const qsizetype archiveMarker = path.indexOf(QLatin1Char('#'));
  const QString file = archiveMarker < 0 ? path : path.left(archiveMarker);
  const QString canonical = QFileInfo(file).canonicalFilePath();
  return canonical.isEmpty() ? QString{}
                             : canonical + (archiveMarker < 0 ? QString{} : path.mid(archiveMarker));
}
}

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
  result["undoRelocation"] = hasRelocation(m_key);
  return result;
}
void LibraryRepair::setSource(const QString& value) {
  m_selected.clear();
  m_source = value;
  m_key.clear();
  refresh();
}
void LibraryRepair::setReason(const QString& value) {
  if (!QStringList{"", "identification", "artwork", "unavailable", "missing-file",
                   "missing-storage", "runtime", "source-error", "duplicates"}.contains(value))
    return;
  m_selected.clear();
  m_reason = value;
  m_key.clear();
  refresh();
}
void LibraryRepair::refresh() {
  m_active = true;
  int previousIndex = -1;
  for (int index = 0; index < m_entries.size(); ++index)
    if (m_entries.at(index).toMap().value("metadataKey").toString() == m_key) {
      previousIndex = index;
      break;
    }
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
    if (reasons.isEmpty() || (!m_source.isEmpty() && source != m_source) ||
        (!m_reason.isEmpty() && !ReviewAvailability::matchesFilter(m_reason, reasons)))
      continue;
    m_entries.append(game);
  }
  QSet<QString> remaining;
  for (const auto& entry : m_entries)
    remaining.insert(entry.toMap().value("metadataKey").toString());
  m_selected.intersect(remaining);
  m_sources.sort();
  int currentIndex = -1;
  for (int index = 0; index < m_entries.size(); ++index)
    if (m_entries.at(index).toMap().value("metadataKey").toString() == m_key) {
      currentIndex = index;
      break;
    }
  if (m_entries.isEmpty()) {
    m_current.clear();
    m_message = "Nothing left to review in these filters";
  } else {
    if (currentIndex < 0)
      currentIndex = previousIndex < 0 ? 0 : qBound(0, previousIndex, m_entries.size() - 1);
    m_current = m_entries.at(currentIndex).toMap();
    m_key = m_current.value("metadataKey").toString();
    if (m_message == "Nothing left to review in these filters")
      m_message.clear();
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
QVariantList LibraryRepair::reasonDetailsFor(const QString& key) const {
  for (int row = 0; row < m_games->rowCount(); ++row)
    if (m_games->index(row).data(GameRoles::MetadataKey).toString() == key)
      return m_games->reviewReasonDetails(row);
  return {};
}
void LibraryRepair::recheck() {
  if (m_key.isEmpty())
    return;
  QString storageRoot;
  for (const QVariant& value : m_current.value(QStringLiteral("reasonDetails")).toList()) {
    const QVariantMap detail = value.toMap();
    if (detail.value(QStringLiteral("key")).toString() == QStringLiteral("missing-storage")) {
      storageRoot = detail.value(QStringLiteral("detail")).toString();
      break;
    }
  }
  QStringList keys = storageRoot.isEmpty() ? QStringList{m_key}
                                           : m_games->reviewKeysUnderPath(storageRoot);
  if (!keys.contains(m_key))
    keys.append(m_key);
  m_games->recheckAvailability(keys);
  m_message = storageRoot.isEmpty() ? "Rechecking this game's availability."
                                    : "Rechecking games under the missing folder.";
  emit changed();
}
bool LibraryRepair::retrySource() {
  if (!m_current.value(QStringLiteral("reasons")).toStringList().contains(
          QStringLiteral("source-error")))
    return false;
  const bool started = m_games->refreshSource(m_current.value(QStringLiteral("source")).toString());
  m_message = started ? "Retrying this source scan."
                      : "The source scan could not be retried.";
  emit changed();
  return started;
}
QVariantMap LibraryRepair::previewRelocation(const QString& key, const QString& newPath) const {
  QVariantMap result{{QStringLiteral("ok"), false},
                     {QStringLiteral("oldPath"), QString{}},
                     {QStringLiteral("newPath"), newPath},
                     {QStringLiteral("refusal"), QString{}},
                     {QStringLiteral("backupMessages"), QStringList{}}};
  const auto refuse = [&result](const QString& reason) {
    result[QStringLiteral("refusal")] = reason;
    return result;
  };
  if (key.isEmpty())
    return refuse("This game is no longer available in the library.");
  QVariantMap game;
  for (int row = 0; row < m_games->rowCount(); ++row) {
    const QVariantMap candidate = m_games->reviewGame(row);
    if (candidate.value(QStringLiteral("metadataKey")).toString() == key) {
      game = candidate;
      break;
    }
  }
  if (game.isEmpty())
    return refuse("This game is no longer available in the library.");
  const QString source = game.value(QStringLiteral("source")).toString();
  if (!GameLauncher::isEmulatorSourceName(source))
    return refuse("Only emulator installations can be relocated here.");
  const QString oldPath = m_launcher ? m_launcher->effectivePath(game)
                                     : game.value(QStringLiteral("installPath")).toString();
  result[QStringLiteral("oldPath")] = oldPath;
  if (oldPath.isEmpty())
    return refuse("The current game path is unavailable.");
  if (newPath.isEmpty())
    return refuse("Choose a new game file.");
  const QFileInfo newInfo(newPath);
  if (!newInfo.isAbsolute() || !GameLauncher::contentAvailable(newPath))
    return refuse("Choose an existing absolute game file.");
  const auto* console = ConsoleCatalog::find(game.value(QStringLiteral("system")).toString());
  const QString physicalPath = !QFileInfo::exists(newPath) && newPath.contains(QLatin1Char('#'))
                                   ? newPath.left(newPath.indexOf(QLatin1Char('#')))
                                   : newPath;
  if (console && !console->extensions.isEmpty() && QFileInfo(physicalPath).isDir())
    return refuse("Choose a game file, not a folder.");
  const QString typeRefusal = GameLauncher::contentTypeRefusal(game, newPath);
  if (!typeRefusal.isEmpty())
    return refuse(typeRefusal);
  const QString canonicalNew = canonicalContentPath(newPath);
  if (canonicalNew.isEmpty())
    return refuse("The selected game file could not be resolved safely.");
  if (canonicalContentPath(oldPath) == canonicalNew)
    return refuse("This game already uses that file.");
  for (int row = 0; row < m_games->rowCount(); ++row) {
    const QVariantMap candidate = m_games->reviewGame(row);
    if (candidate.value(QStringLiteral("metadataKey")).toString() == key)
      continue;
    const QString path = m_launcher ? m_launcher->effectivePath(candidate)
                                   : candidate.value(QStringLiteral("installPath")).toString();
    if (!path.isEmpty() && canonicalContentPath(path) == canonicalNew)
      return refuse(QStringLiteral("Already in your library as “%1”. Link them from Manage instead.")
                        .arg(candidate.value(QStringLiteral("title")).toString()));
  }
  const QVariantMap priorState = m_state.value(relocationStateKey(key)).toMap();
  for (const QVariant& value : priorState.value(QStringLiteral("copyReceipts")).toList()) {
    const QVariantMap receipt = value.toMap();
    if (receipt.value(QStringLiteral("newGame")).toString() != newPath)
      continue;
    if (!m_saveBackups || !m_saveBackups->relocationReceiptMatches(newPath, receipt))
      return refuse("Save backups already exist at the new location.");
    if (!m_saveBackups->relocationSourceMatches(oldPath, receipt))
      return refuse("Save backups at the previous location changed. Review them before relocating again.");
    QStringList backupMessages{QStringLiteral("Earlier copied save backups will be used.")};
    if (!receipt.value(QStringLiteral("sharedKeys")).toStringList().isEmpty())
      backupMessages.append(
          QStringLiteral("Shared save backups stay with their shared files and are linked to the new location."));
    result[QStringLiteral("backupMessages")] = backupMessages;
    result[QStringLiteral("hasBackups")] = true;
    result[QStringLiteral("reuseBackupCopy")] = true;
    result[QStringLiteral("backupReceipt")] = receipt;
    result[QStringLiteral("ok")] = true;
    return result;
  }
  if (m_saveBackups) {
    const QVariantMap backupPreview = m_saveBackups->previewRelocationBackups(oldPath, newPath);
    if (!backupPreview.value(QStringLiteral("ok")).toBool())
      return refuse(backupPreview.value(QStringLiteral("refusal")).toString());
    QStringList backupMessages;
    if (backupPreview.value(QStringLiteral("copyable")).toBool())
      backupMessages.append(
          QStringLiteral("Save backups are copied to the new location; the originals stay where they are."));
    if (backupPreview.value(QStringLiteral("hasShared")).toBool())
      backupMessages.append(
          QStringLiteral("Shared save backups stay with their shared files and are linked to the new location."));
    result[QStringLiteral("backupMessages")] = backupMessages;
    result[QStringLiteral("hasBackups")] = backupPreview.value(QStringLiteral("hasBackups"));
  }
  result[QStringLiteral("ok")] = true;
  return result;
}
bool LibraryRepair::relocate(const QString& key, const QString& newPath) {
  const QVariantMap preview = previewRelocation(key, newPath);
  if (!preview.value(QStringLiteral("ok")).toBool()) {
    m_message = preview.value(QStringLiteral("refusal")).toString();
    emit changed();
    return false;
  }
  if (!m_launcher) {
    m_message = "Launch setup is unavailable. The game path was not changed.";
    emit changed();
    return false;
  }
  QVariantMap game;
  for (int row = 0; row < m_games->rowCount(); ++row) {
    const QVariantMap candidate = m_games->reviewGame(row);
    if (candidate.value(QStringLiteral("metadataKey")).toString() == key) {
      game = candidate;
      break;
    }
  }
  if (game.isEmpty()) {
    m_message = "This game is no longer available in the library.";
    emit changed();
    return false;
  }
  const QString oldPath = preview.value(QStringLiteral("oldPath")).toString();
  const bool reuseBackupCopy = preview.value(QStringLiteral("reuseBackupCopy")).toBool();
  QVariantMap copyReceipt = preview.value(QStringLiteral("backupReceipt")).toMap();
  QString error;
  if (m_saveBackups && !reuseBackupCopy &&
      !m_saveBackups->copyRelocationBackups(oldPath, newPath, &copyReceipt, &error)) {
    m_message = error;
    emit changed();
    return false;
  }
  const bool createdBackupCopy = !reuseBackupCopy && !copyReceipt.isEmpty();
  const QString stateKey = relocationStateKey(key);
  const QVariantMap previousState = m_state.value(stateKey).toMap();
  const QVariantMap previousSetup = m_launcher->setupOverride(game);
  QVariantList copyReceipts = previousState.value(QStringLiteral("copyReceipts")).toList();
  if (createdBackupCopy)
    copyReceipts.append(copyReceipt);
  QVariantMap snapshot = previousState;
  snapshot.insert(QStringLiteral("hasSetup"), true);
  snapshot.insert(QStringLiteral("previousSetupExists"), !previousSetup.isEmpty());
  if (previousSetup.isEmpty())
    snapshot.remove(QStringLiteral("setup"));
  else
    snapshot.insert(QStringLiteral("setup"), previousSetup);
  if (copyReceipts.isEmpty())
    snapshot.remove(QStringLiteral("copyReceipts"));
  else
    snapshot.insert(QStringLiteral("copyReceipts"), copyReceipts);
  const auto restorePreviousState = [this, &stateKey, &previousState] {
    if (previousState.isEmpty())
      m_state.remove(stateKey);
    else
      m_state.setValue(stateKey, previousState);
    m_state.sync();
  };
  const auto rollbackNewCopy = [this, &newPath, &copyReceipt, createdBackupCopy](QString* error) {
    return !createdBackupCopy || !m_saveBackups ||
           m_saveBackups->rollbackRelocationBackups(newPath, copyReceipt, error);
  };
  m_state.setValue(stateKey, snapshot);
  m_state.sync();
  if (m_state.status() != QSettings::NoError) {
    restorePreviousState();
    QString rollbackError;
    const bool rolledBack = rollbackNewCopy(&rollbackError);
    m_message = rolledBack ? "Could not save the relocation undo point. The game path was not changed."
                           : "Could not save the relocation undo point or remove the copied backups: " +
                                 rollbackError;
    emit changed();
    return false;
  }
  const QString mode = previousSetup.isEmpty()
                           ? QStringLiteral("Automatic")
                           : previousSetup.value(QStringLiteral("mode"), QStringLiteral("Automatic"))
                                 .toString();
  const QString core = previousSetup.value(QStringLiteral("core")).toString();
  const bool flatpak = previousSetup.isEmpty()
                           ? game.value(QStringLiteral("flatpak")).toBool()
                           : previousSetup.value(QStringLiteral("flatpak")).toBool();
  if (!m_launcher->saveSetup(game, mode, core, flatpak, newPath)) {
    const QString setupError = m_launcher->lastError();
    restorePreviousState();
    QString rollbackError;
    const bool rolledBack = rollbackNewCopy(&rollbackError);
    m_message = rolledBack ? setupError : setupError + " " + rollbackError;
    emit changed();
    return false;
  }
  m_games->recheckAvailability({key});
  m_message = "Game path updated. The original files and save backups were kept.";
  refresh();
  return true;
}
bool LibraryRepair::undoRelocation(const QString& key) {
  const QString stateKey = relocationStateKey(key);
  if (!hasRelocation(key) || !m_launcher) {
    m_message = "There is no saved relocation to undo.";
    emit changed();
    return false;
  }
  QVariantMap game;
  for (int row = 0; row < m_games->rowCount(); ++row) {
    const QVariantMap candidate = m_games->reviewGame(row);
    if (candidate.value(QStringLiteral("metadataKey")).toString() == key) {
      game = candidate;
      break;
    }
  }
  if (game.isEmpty()) {
    m_message = "This game is no longer available in the library.";
    emit changed();
    return false;
  }
  QVariantMap snapshot = m_state.value(stateKey).toMap();
  const bool restored = snapshot.value(QStringLiteral("previousSetupExists")).toBool()
                            ? m_launcher->restoreSetupSnapshot(
                                  game, snapshot.value(QStringLiteral("setup")).toMap())
                            : m_launcher->resetSetup(game);
  if (!restored) {
    m_message = m_launcher->lastError();
    emit changed();
    return false;
  }
  snapshot.remove(QStringLiteral("hasSetup"));
  snapshot.remove(QStringLiteral("previousSetupExists"));
  snapshot.remove(QStringLiteral("setup"));
  if (snapshot.value(QStringLiteral("copyReceipts")).toList().isEmpty())
    m_state.remove(stateKey);
  else
    m_state.setValue(stateKey, snapshot);
  m_state.sync();
  if (m_state.status() != QSettings::NoError) {
    m_message = "The previous setup was restored, but its undo point could not be cleared.";
    emit changed();
    return false;
  }
  m_games->recheckAvailability({key});
  m_message = "Previous launch setup restored. Save backups at both locations were kept.";
  refresh();
  return true;
}
bool LibraryRepair::hasRelocation(const QString& key) const {
  return !key.isEmpty() &&
         m_state.value(relocationStateKey(key)).toMap().value(QStringLiteral("hasSetup")).toBool();
}
