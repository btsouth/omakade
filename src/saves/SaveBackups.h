#pragma once
#include "saves/SaveSetStore.h"
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <functional>

// Local save protection, with legacy SRAM snapshot compatibility.
class SaveBackups final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int retention READ retention NOTIFY changed)
  Q_PROPERTY(int storageLimitMiB READ storageLimitMiB NOTIFY changed)
  Q_PROPERTY(int revision READ revision NOTIFY changed)
  Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
  Q_PROPERTY(qint64 storageBytes READ storageBytes NOTIFY changed)
  Q_PROPERTY(bool canSnapshot READ canSnapshot NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(bool recoveryPending READ recoveryPending NOTIFY changed)
public:
  explicit SaveBackups(QObject* parent = nullptr);
  SaveBackups(QString home, QString config, QString root, std::function<bool()> emulatorRunning,
              QObject* parent = nullptr);
  void setEnabled(bool enabled) { m_enabled = enabled; }
  int retention() const { return m_retention; }
  int storageLimitMiB() const { return m_storageLimitMiB; }
  Q_INVOKABLE bool setPolicy(int retention,int storageLimitMiB);
  Q_INVOKABLE QVariantMap coverage(const QVariantMap& context) const;
  Q_INVOKABLE QVariantMap previewCustomFiles(const QVariantMap& context,const QStringList& files,bool shared) const;
  Q_INVOKABLE bool setCustomFiles(const QVariantMap& context,const QStringList& files,bool shared);
  Q_INVOKABLE bool resetCustomFiles(const QVariantMap& context);
  int revision() const { return m_revision; }
  QVariantList versions() const { return m_versions; }
  qint64 storageBytes() const;
  bool canSnapshot() const;
  QString message() const { return m_message; }
  bool recoveryPending() const { return m_sets.pending(); }
  Q_INVOKABLE bool retryRecovery();
  // Returns the supported existing save path, or empty without guessing.
  QString discover(const QString& game, const QString& core, bool allowMissing = false) const;
  bool protect(const QString& game, const QString& core, bool flatpak = false);
  bool protectLaunch(const QString& source, const QString& game, const QString& core = {},
                     bool flatpak = false, const QString& id = {}, const QString& runner = {},
                     const QString& target = {});
  Q_INVOKABLE void selectLaunch(const QString& source, const QString& game, const QString& core,
                                bool flatpak, const QString& id, const QString& runner,
                                const QString& target);
  Q_INVOKABLE int count(const QString& game) const;
  Q_INVOKABLE void selectGame(const QString& game);
  Q_INVOKABLE bool snapshotSelected();
  Q_INVOKABLE bool deleteVersion(const QString& version);
  Q_INVOKABLE bool restore(const QString& version);
signals:
  void changed();
  void warning(const QString& message);

private:
  SaveLayout resolve(const QJsonObject& context) const;
  QString gameRoot(const QString& game) const;
  QVariantList list(const QString& game) const;
  bool snapshot(const QString& game, const QString& core, const QString& source, QString* error);
  void report(const QString& message, bool warn = false);
  QString layoutFile() const;
  bool writeLayout(const QVariantMap& context,const QStringList& files,bool shared,bool remove);
  int m_retention=10, m_storageLimitMiB=2048;
  bool m_customPolicy = false;
  QString m_home, m_config, m_root, m_game, m_message;
  QJsonObject m_context;
  std::function<bool()> m_running;
  SaveSetStore m_sets;
  bool m_enabled = true;
  int m_revision = 0;
  QVariantList m_versions;
};
