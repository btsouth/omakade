#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <functional>

// Small control channel, separate from the unchanged authenticated plugin socket.
class GuideClient final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool opened READ opened NOTIFY changed)
  Q_PROPERTY(bool available READ available CONSTANT)
public:
  explicit GuideClient(bool enabled, QObject* parent = nullptr);
  static QString socketPath();
  static void ensureResident(QObject* owner);
  static void request(const QJsonObject& command, QObject* owner,
                      std::function<void(const QString&, const QJsonObject&)> done = {});
  static void requestShortcut(const QString& node, QObject* owner,
                              std::function<void(const QString&, const QJsonObject&)> done);
  // CLI-only, before QGuiApplication exists. Never call on a GUI event loop.
  static QString routeShortcut(const QString& node);
  bool opened() const { return m_opened; }
  bool available() const { return m_enabled; }
  Q_INVOKABLE bool usable() const { return m_usable; }
  bool hasGame() const { return m_hasGame; }
  Q_INVOKABLE bool toggle(const QString& node = {}, bool fallback = false);
  Q_INVOKABLE void close();
  void publish(const QVariantList& sessions, const QJsonObject& context = {});
signals:
  void changed();
  void summonFailed();
  void libraryRequested();
private:
  bool m_enabled, m_opened = false, m_usable = false, m_hasGame = false;
};
