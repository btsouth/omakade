#pragma once
#include <QObject>
#include <QVariantList>
class UnifiedGameModel;
class GameLauncher;
class SaveBackups;
class SaveProtection final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
  Q_PROPERTY(QVariantList cleanup READ cleanup NOTIFY changed)
  Q_PROPERTY(qint64 storageBytes READ storageBytes NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
  SaveProtection(UnifiedGameModel* games, GameLauncher* launcher, SaveBackups* backups,
                 QObject* parent = nullptr);
  QVariantList entries() const { return m_entries; }
  QVariantList cleanup() const { return m_cleanup; }
  qint64 storageBytes() const { return m_bytes; }
  bool busy() const { return m_busy; }
  Q_INVOKABLE void refresh();
  Q_INVOKABLE bool select(const QString& key);
  Q_INVOKABLE void previewCleanup();
  Q_INVOKABLE bool applyCleanup();
signals:
  void changed();

private:
  UnifiedGameModel* m_games;
  GameLauncher* m_launcher;
  SaveBackups* m_backups;
  QVariantList m_entries, m_cleanup;
  qint64 m_bytes = 0;
  bool m_busy = false;
};
