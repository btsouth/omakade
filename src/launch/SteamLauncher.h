#pragma once

#include <QObject>
#include <QUrl>

class SteamLauncher final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
  explicit SteamLauncher(QObject* parent = nullptr);

  [[nodiscard]] QString lastError() const;
  [[nodiscard]] static QUrl launchUrl(const QString& appId);
  [[nodiscard]] static QUrl manageUrl(const QString& appId);
  [[nodiscard]] static QUrl installUrl(const QString& appId);

  Q_INVOKABLE bool launch(const QString& appId);
  Q_INVOKABLE bool manage(const QString& appId);
  Q_INVOKABLE bool install(const QString& appId);

signals:
  void lastErrorChanged();

private:
  bool open(const QUrl& url);
  QString m_lastError;
};
