#include "selftest/SelfTest.h"

#include "app/RestoreStartup.h"
#include "backup/BackupDatabase.h"
#include "backup/BackupSnapshot.h"
#include "backup/BackupStartup.h"
#include "library/LibraryFilterModel.h"
#include "theme/OmarchyTheme.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>
#include <QTemporaryDir>
#include <QTimer>

namespace SelfTest {

int testRestoreStartup(QGuiApplication& application, OmarchyTheme& theme, const QString& mode,
                       bool couch, const QSize& size, const QString& screenshot) {
  if (!QStringList{"success", "retry", "undo", "close"}.contains(mode)) return EXIT_FAILURE;
  QTemporaryDir temporary;
  if (!temporary.isValid()) return EXIT_FAILURE;
  const BackupRecovery::Paths paths{temporary.filePath("library.sqlite3"),
      temporary.filePath("config.toml"), temporary.filePath("recovery")};
  BackupPayload original;
  original.createdAt = "2026-09-05T00:00:00Z";
  original.library = {{"collections", QJsonArray{QJsonObject{{"name", "Original"}, {"created_at", 1}}}}};
  BackupPayload incoming = original;
  incoming.library = {{"collections", QJsonArray{QJsonObject{{"name", "Imported"}, {"created_at", 2}}}}};
  QString error;
  if (!BackupDatabase::restore(paths.database, original, BackupDatabase::Mode::Replace, &error) ||
      !BackupRecovery(paths).stage(incoming, BackupDatabase::Mode::Replace, &error)) {
    qCritical().noquote() << error;
    return EXIT_FAILURE;
  }
  QFile journal(paths.state + "/active.json");
  if (!journal.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
  const QString job = QJsonDocument::fromJson(journal.readAll()).object().value("id").toString();
  const QString stagedPath = paths.state + '/' + job + "/incoming.omakade-backup";
  QFile staged(stagedPath);
  if (!staged.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
  const QByteArray stagedBytes = staged.readAll();
  staged.close();
  if (mode != "success") {
    if (!staged.open(QIODevice::WriteOnly) || staged.write("invalid") != 7) return EXIT_FAILURE;
    staged.close();
  }
  bool failed = false;
  bool sawError = false;
  bool handled = false;
  const bool resolved = runRestoreStartup(application, theme, paths, couch,
      [&](QQuickWindow* window, BackupStartup* recovery) {
        if (size.isValid()) window->resize(size);
        const auto fail = [&, window](const QString& text) {
          qCritical().noquote() << text;
          failed = true;
          window->close();
        };
        QTimer::singleShot(10000, window, [fail] { fail("Restore startup fixture timed out"); });
        QObject::connect(recovery, &BackupStartup::changed, window, [&, window, recovery, fail] {
          if (recovery->busy() || mode == "success" || handled) return;
          handled = true;
          sawError = recovery->canUndo() && recovery->message().contains("changed");
          QTimer::singleShot(80, window, [&, window, recovery, fail] {
            auto* retry = window->findChild<QQuickItem*>("restoreRetryButton");
            auto* undo = window->findChild<QQuickItem*>("restoreUndoButton");
            if (!sawError || !retry || !undo || !retry->hasActiveFocus()) {
              fail("Restore failure did not keep the library closed with usable retry/undo focus");
              return;
            }
            if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot)) {
              fail("Could not render the restore recovery screen");
              return;
            }
            const auto navigate = [window](int key) {
              QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QVariant(key)));
            };
            if (mode == "close") { navigate(Qt::Key_Escape); return; }
            navigate(Qt::Key_Down);
            auto* folder = window->findChild<QQuickItem*>("restoreFolderButton");
            if (!folder || !folder->hasActiveFocus()) { fail("Restore Down did not follow the button grid"); return; }
            navigate(Qt::Key_Up);
            navigate(Qt::Key_Right);
            if (!undo->hasActiveFocus()) { fail("Restore controller navigation did not reach Undo"); return; }
            if (mode == "retry") {
              QFile repair(stagedPath);
              if (!repair.open(QIODevice::WriteOnly) || repair.write(stagedBytes) != stagedBytes.size()) {
                fail("Could not repair the test archive"); return;
              }
              repair.close();
              navigate(Qt::Key_Left);
            }
            navigate(Qt::Key_Return);
          });
        });
      });
  if (failed || resolved != (mode != "close") || (mode != "success" && !sawError)) return EXIT_FAILURE;
  BackupPayload actual;
  if (!BackupSnapshot::capture(paths.database, {}, &actual, &error)) return EXIT_FAILURE;
  const QString expected = mode == "success" || mode == "retry" ? "Imported" : "Original";
  if (actual.library.value("collections").toArray().first().toObject().value("name").toString() != expected)
    return EXIT_FAILURE;
  if (resolved) {
    // Prove the temporary startup window did not leave an application quit
    // event that prevents the next window from entering its event loop.
    QQuickWindow nextWindow;
    nextWindow.show();
    bool entered = false;
    QTimer::singleShot(50, &application, [&] { entered = true; application.quit(); });
    application.exec();
    if (!entered) return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

bool parseRenderSize(const QString& value, QSize* result) {
  const QStringList dimensions = value.split(QLatin1Char('x'));
  bool widthValid = false;
  bool heightValid = false;
  const int width = dimensions.value(0).toInt(&widthValid);
  const int height = dimensions.value(1).toInt(&heightValid);
  if (dimensions.size() != 2 || !widthValid || !heightValid || width <= 0 || height <= 0 ||
      width > 16384 || height > 16384) {
    return false;
  }
  *result = QSize(width, height);
  return true;
}

} // namespace SelfTest
