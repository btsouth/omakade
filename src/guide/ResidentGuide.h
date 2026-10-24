#pragma once
#include "guide/InGameGuide.h"
#include "highlights/HighlightService.h"
#include <QLocalServer>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>
#include <memory>

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
  // Omakade's replay buffer, following the same game as the guide. Absent without the guide.
  std::unique_ptr<HighlightService> m_highlights;
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
  // Home after the guide's Desktop: the pad to reopen the guide with once Game Mode is back.
  QString m_reopenNode;
  QElapsedTimer m_reopenClock;
  int m_refreshGeneration = 0;
  bool m_refreshing = false, m_ready = false, m_locked = false, m_provisioned = false, m_provisioning = false;
  // The game is the focused window, so it is in view even while Game Mode is parked.
  bool m_gameFocused = false;
  QJsonObject command(const QJsonObject& command);
  void fallback();
  // Runs the omakade command line in the desktop session, outside this service.
  void launch(const QString& argument);
};
