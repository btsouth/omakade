#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

#include "launch/LaunchCommand.h"

class SteamLauncher final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
  explicit SteamLauncher(QObject* parent = nullptr);

  [[nodiscard]] QString lastError() const;
  [[nodiscard]] static QUrl launchUrl(const QString& appId);
  [[nodiscard]] static QUrl manageUrl(const QString& appId);
  [[nodiscard]] static QUrl installUrl(const QString& appId);
  // Commands that hand a steam:// URL to the Steam client directly, in the order
  // they should be tried: the native executable, then Flatpak Steam. Empty when
  // neither is available.
  [[nodiscard]] static QList<LaunchCommand>
  steamCommands(const QUrl& url, const QString& steamExecutable, bool flatpakSteamInstalled);
  // Opens a steam:// URL through the Steam client, falling back to the desktop URL
  // handler. Some Steam packages register no x-scheme-handler, in which case the
  // handler fallback alone would open a web browser instead of Steam.
  [[nodiscard]] static bool openUrl(const QUrl& url);
  // Commands that start the Steam client minimized to its tray, in the same order as
  // steamCommands. Empty when Steam is not installed.
  [[nodiscard]] static QList<LaunchCommand> backgroundCommands(const QString& steamExecutable,
                                                               bool flatpakSteamInstalled);
  [[nodiscard]] static bool installed();
  // True while a Steam client, or its launcher script, is running for this user.
  [[nodiscard]] static bool clientRunning(const QString& procRoot = QStringLiteral("/proc"));
  // Starts Steam minimized unless it is already running, so a later steam:// launch
  // reaches a client that is ready. False only when no command could be started.
  static bool startInBackground();

  Q_INVOKABLE bool launch(const QString& appId);
  Q_INVOKABLE bool manage(const QString& appId);
  Q_INVOKABLE bool install(const QString& appId);

signals:
  void lastErrorChanged();

private:
  bool open(const QUrl& url);
  QString m_lastError;
};
