#pragma once
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QVariantList>
class UnifiedGameModel;
class GameMetadata;
class LibraryRepair final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
  Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
  Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
  Q_PROPERTY(QString reason READ reason WRITE setReason NOTIFY changed)
  Q_PROPERTY(QStringList sources READ sources NOTIFY changed)
  Q_PROPERTY(QStringList selectedTitles READ selectedTitles NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
public:
  LibraryRepair(UnifiedGameModel*, GameMetadata*, const QString& statePath,
                QObject* parent = nullptr);
  QVariantList entries() const { return m_entries; }
  QVariantMap current() const;
  QString source() const { return m_source; }
  QString reason() const { return m_reason; }
  QStringList sources() const { return m_sources; }
  QString message() const { return m_message; }
  void setSource(const QString&);
  void setReason(const QString&);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void pause() { m_active = false; }
  Q_INVOKABLE void move(int delta);
  Q_INVOKABLE bool checkpoint(const QString& kind);
  Q_INVOKABLE bool undo(const QString& kind);
  Q_INVOKABLE void retry(bool all);
  Q_INVOKABLE void toggleSelected();
  Q_INVOKABLE void retrySelected();
  Q_INVOKABLE QStringList reasonsFor(const QString& key) const;
  QStringList selectedTitles() const;
signals:
  void changed();

private:
  void save();
  UnifiedGameModel* m_games;
  GameMetadata* m_metadata;
  QSettings m_state;
  QVariantList m_entries;
  QVariantMap m_current;
  QString m_key, m_source, m_reason, m_message;
  QStringList m_sources;
  QSet<QString> m_selected;
  bool m_active = false;
};
