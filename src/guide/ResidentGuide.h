#pragma once
#include "guide/InGameGuide.h"
#include <QLocalServer>
#include <QJsonArray>
#include <QTimer>

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
  QTimer m_refresh;
  QJsonArray m_published;
  int m_refreshGeneration = 0;
  bool m_refreshing = false, m_ready = false, m_locked = false, m_provisioned = false;
  QJsonObject command(const QJsonObject& command);
  void fallback();
};
