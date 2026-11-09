#include "app/RestoreStartup.h"

#include "backup/BackupStartup.h"
#include "input/ControllerInput.h"
#include "theme/OmarchyTheme.h"

#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

bool runRestoreStartup(QGuiApplication& application, OmarchyTheme& theme,
                       const BackupRecovery::Paths& paths, bool couchMode,
                       const std::function<void(QQuickWindow*, BackupStartup*)>& ready) {
  const QString phase = BackupRecovery(paths).status();
  if (phase == "none" || phase == "complete" || phase == "reverted") return true;
  BackupStartup recovery(paths);
  ControllerInput controller;
  QEventLoop loop;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("Theme", &theme);
  engine.rootContext()->setContextProperty("Recovery", &recovery);
  engine.rootContext()->setContextProperty("RecoveryController", &controller);
  bool resolved = false;
  QObject::connect(&recovery, &BackupStartup::resolved, &loop, [&] {
    resolved = true;
    loop.quit();
  });
  engine.loadFromModule("Omakade", "RestoreStartup");
  if (engine.rootObjects().isEmpty()) return false;
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (!window) return false;
  window->setProperty("couchMode", couchMode);
  if (couchMode && !ready) window->showFullScreen();
  QObject::connect(window, &QQuickWindow::closing, &loop, [&] {
    if (!recovery.busy()) loop.quit();
  });
  const auto syncInput = [&] {
    controller.setInputEnabled(application.applicationState() == Qt::ApplicationActive &&
                               application.focusWindow() == window);
  };
  QObject::connect(&application, &QGuiApplication::applicationStateChanged, &controller, syncInput);
  QObject::connect(&application, &QGuiApplication::focusWindowChanged, &controller, syncInput);
  syncInput();
  if (!ready) controller.start();
  // Closing the temporary startup window must not queue an application quit
  // before the actual library window has been constructed.
  const bool quitOnClose = application.quitOnLastWindowClosed();
  application.setQuitOnLastWindowClosed(false);
  if (ready) ready(window, &recovery);
  QTimer::singleShot(0, &recovery, &BackupStartup::retry);
  loop.exec();
  window->hide();
  application.setQuitOnLastWindowClosed(quitOnClose);
  return resolved;
}
