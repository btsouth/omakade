#pragma once
#include "guide/InGameGuide.h"
#include <QLocalServer>
#include <QJsonArray>
#include <QTimer>
#include <atomic>

// Constructed inside sessiond's dedicated guide thread. No library, GUI or QML engine.
class ResidentGuide final : public QObject {
  Q_OBJECT
public:
  explicit ResidentGuide(QObject* parent = nullptr);
  void refresh();
signals:
  void snapshotReady();
private:
  InGameGuide m_guide;
  QLocalServer m_control;
  QTimer m_refresh, m_debounce, m_reconnect;
  QLocalSocket m_events;
  QByteArray m_eventBuffer;
  QProcessEnvironment m_environment = QProcessEnvironment::systemEnvironment();
  bool m_resolving = false, m_refreshPending = false;
  std::shared_ptr<GuidePlugin::RetryState> m_provisionRetry = std::make_shared<GuidePlugin::RetryState>();
  std::shared_ptr<std::atomic_uint> m_desktopGeneration = std::make_shared<std::atomic_uint>(0);
  void connectEvents();
  QJsonArray m_published;
  int m_refreshGeneration = 0;
  bool m_refreshing = false, m_ready = false, m_locked = false, m_provisioned = false, m_provisioning = false;
  QJsonObject command(const QJsonObject& command);
  void fallback();
};
