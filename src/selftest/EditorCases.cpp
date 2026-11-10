#include "selftest/SelfTest.h"

#include "selftest/SelfTestSupport.h"
#include "backup/BackupManager.h"
#include "input/ControllerInput.h"
#include "library/GameRoles.h"
#include "library/LibraryFilterModel.h"
#include "library/ManualGameModel.h"
#include "library/UnifiedGameModel.h"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <memory>

namespace SelfTest {

void driveBackupEditor(Harness& harness, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  BackupRecovery::Paths& managerPaths = *app.managerPaths;
  BackupManager& backups = *app.backups;
  QWindow*& rootWindow = *app.rootWindow;
  QTemporaryDir& artworkFixture = harness.fixtures.artworkFixture;
  QString& backupFixturePath = harness.fixtures.backupFixturePath;
  auto* timer = new QTimer(rootWindow);
  timer->setInterval(80);
  auto step = std::make_shared<int>(0);
  const QString exportPath = artworkFixture.filePath("export.omakade-backup");
  QObject::connect(timer, &QTimer::timeout, rootWindow,
      [&, timer, step, exportPath] {
    const auto fail = [&](const QString& message) { timer->stop(); qCritical().noquote() << message; application.exit(EXIT_FAILURE); };
    auto* editor = rootWindow->findChild<QQuickItem*>("backupEditor");
    auto* field = rootWindow->findChild<QQuickItem*>("backupPathField");
    const auto press = [&](const QString& name) {
      auto* button = rootWindow->findChild<QQuickItem*>(name);
      if (!button || !button->isVisible() || !button->isEnabled()) return false;
      button->forceActiveFocus();
      controller.acceptRequested();
      return true;
    };
    if (!editor || !field) { fail("Backup editor controls are missing"); return; }
    if (*step == 0) {
      rootWindow->setProperty("diagnosticsOpen", true);
      // Backup and restore lives in the About & storage section of the redesigned settings.
      auto* settingsOverlay = rootWindow->findChild<QQuickItem*>("settingsOverlay");
      if (settingsOverlay == nullptr) { fail("Settings panel is missing"); return; }
      settingsOverlay->setProperty("section", 4);
      if (!press("backupSettingsButton") || !rootWindow->property("backupEditorOpen").toBool()) { fail("Settings could not open the backup editor"); return; }
      ++*step;
    } else if (*step == 1) {
      if (rootWindow->property("couchMode").toBool()) {
        field->forceActiveFocus(); controller.acceptRequested();
        if (!rootWindow->property("couchTextEntryOpen").toBool()) { fail("Backup path cannot use controller text entry"); return; }
        QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
      }
      field->setProperty("text", exportPath);
      if (!press("backupExportButton") || editor->property("pendingMode").toString() != "export") { fail("Backup export did not request confirmation"); return; }
      ++*step;
    } else if (*step == 2) {
      if (!press("backupConfirmButton")) { fail("Backup save confirmation is missing"); return; }
      ++*step;
    } else if (*step == 3 && !backups.busy()) {
      BackupPayload exported; QString error;
      if (!BackupArchive::read(exportPath, &exported, &error)) { fail("UI export failed: " + backups.message()); return; }
      field->setProperty("text", backupFixturePath);
      if (!press("backupPreviewButton")) { fail("Backup preview action is missing"); return; }
      ++*step;
    } else if (*step == 4 && !backups.busy()) {
      if (!backups.hasPreview()) { fail("UI preview failed: " + backups.message()); return; }
      auto* text = rootWindow->findChild<QQuickItem*>("backupPreviewText");
      auto* scroll = rootWindow->findChild<QQuickItem*>("backupPreviewScroll");
      if (!text || !scroll) { fail("Backup preview content is missing"); return; }
      const QString previewText = text->property("text").toString();
      if (!previewText.contains("NEW OR CHANGED LAUNCH COMMANDS (1)") || !previewText.contains("\"Restored shell game\" (new)") ||
          !previewText.contains("WARNING: runs code written into its arguments") || !previewText.contains("Arguments: \"-c\" \"echo restored\"")) {
        fail("Backup preview does not list the incoming launch command"); return;
      }
      auto* flickable = scroll->property("contentItem").value<QObject*>();
      if (!flickable) { fail("Backup preview viewport is missing"); return; }
      // Preview data can arrive before the ScrollView has laid out its text.
      // Wait for an overflowing viewport before testing the controller gesture;
      // the existing fixture deadline still bounds this readiness check.
      if (!rootWindow->isActive() || !scroll->isVisible() ||
          flickable->property("height").toReal() <= 0 ||
          flickable->property("contentHeight").toReal() <=
              flickable->property("height").toReal()) {
        return;
      }
      text->forceActiveFocus();
      controller.focusDirectionRequested(Qt::Key_Down);
      if (flickable->property("contentY").toReal() <= 0) { fail("Controller could not scroll the backup preview"); return; }
      controller.focusDirectionRequested(Qt::Key_Right);
      auto* merge = rootWindow->findChild<QQuickItem*>("backupMergeButton");
      if (!merge || !merge->hasActiveFocus()) { fail("Preview cannot reach restore choices"); return; }
      controller.acceptRequested();
      if (editor->property("pendingMode").toString() != "merge" || BackupRecovery(managerPaths).status() != "none") { fail("Merge must await explicit confirmation"); return; }
      ++*step;
    } else if (*step == 5) {
      auto* mergeText = rootWindow->findChild<QQuickItem*>("backupConfirmText");
      if (!mergeText || !mergeText->property("text").toString().contains("will be able to run when you choose Play")) { fail("Merge confirmation does not warn about launch commands"); return; }
      if (!press("backupCancelButton") || !editor->property("pendingMode").toString().isEmpty()) { fail("Restore confirmation could not cancel"); return; }
      if (!press("backupReplaceButton") || editor->property("pendingMode").toString() != "replace") { fail("Replace confirmation is missing"); return; }
      auto* confirmText = rootWindow->findChild<QQuickItem*>("backupConfirmText");
      if (!confirmText || !confirmText->property("text").toString().contains("will be able to run when you choose Play")) { fail("Replace confirmation does not warn about launch commands"); return; }
      ++*step;
    } else if (*step == 6) {
      if (!press("backupConfirmButton")) { fail("Replacement could not be confirmed"); return; }
      ++*step;
    } else if (*step == 7 && !backups.busy()) {
      if (!editor->property("queued").toBool() || BackupRecovery(managerPaths).status() != "queued") { fail("Confirmed restore was not queued: " + backups.message()); return; }
      auto* close = rootWindow->findChild<QQuickItem*>("backupCloseAppButton");
      if (!close || !close->isVisible()) { fail("Queued restore did not show Close Omakade"); return; }
      // The editor moves focus with Qt.callLater, so this tick can land first.
      // The fixture deadline still fails the test if focus never arrives.
      if (!close->hasActiveFocus()) return;
      timer->stop(); application.quit();
    }
  });
  QTimer::singleShot(150, timer, [timer] { timer->start(); });
  QTimer::singleShot(10000, timer, [&application] { qCritical() << "Backup editor fixture timed out"; application.exit(EXIT_FAILURE); });
}

void driveBulkEditor(Harness&, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  LibraryFilterModel& library = *app.library;
  QWindow*& rootWindow = *app.rootWindow;
  QTimer::singleShot(150, &application, [&application, rootWindow, &library, &controller] {
    const auto fail = [&application](const QString& message) { qCritical() << message; application.exit(EXIT_FAILURE); };
    QMetaObject::invokeMethod(rootWindow, "openBulkOrganization");
    auto* editor = rootWindow->findChild<QQuickItem*>(QStringLiteral("bulkOrganizationEditor"));
    if (!editor || !editor->isVisible()) { fail("Bulk organization editor is missing"); return; }
    QMetaObject::invokeMethod(editor, "focusRow", Q_ARG(QVariant, 0));
    controller.acceptRequested();
    for (int i = 0; i < 25; ++i) controller.focusDirectionRequested(Qt::Key_Down);
    if (editor->property("focusedRow").toInt() != 25) { fail("Bulk selection cannot scroll with a controller"); return; }
    controller.acceptRequested();
    if (library.selectionCount() != 2) { fail("Bulk game selection did not toggle two games"); return; }
    if (editor->property("stacked").toBool()) {
      auto* favorite = rootWindow->findChild<QQuickItem*>(QStringLiteral("bulkFavoriteButton"));
      QMetaObject::invokeMethod(editor, "focusRow", Q_ARG(QVariant, library.rowCount() - 1));
      controller.focusDirectionRequested(Qt::Key_Down);
      if (!favorite || !favorite->hasActiveFocus()) { fail("Cannot leave the stacked game list for actions"); return; }
      controller.focusDirectionRequested(Qt::Key_Up);
      if (favorite->hasActiveFocus() || editor->property("focusedRow").toInt() != library.rowCount() - 1) {
        fail("Cannot return from stacked actions to the game list"); return;
      }
    }
    QTimer::singleShot(100, &application, [&application, rootWindow, editor, &library, &controller, fail] {
      const QString fieldError = verifyEditorTextFields(qobject_cast<QQuickWindow*>(rootWindow), editor, controller);
      if (!fieldError.isEmpty()) { fail(fieldError); return; }
      if (rootWindow->property("couchMode").toBool()) {
        auto* tags = rootWindow->findChild<QQuickItem*>(QStringLiteral("bulkTagsField"));
        if (!tags) { fail("Bulk tags field is missing"); return; }
        tags->forceActiveFocus();
        controller.acceptRequested();
        if (!rootWindow->property("couchTextEntryOpen").toBool()) { fail("Bulk tags cannot open controller text entry"); return; }
        QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
      }
      const int before = library.rowCount();
      const QVariantMap changes{{"hidden", true}, {"tagsAdd", "tested"}};
      QMetaObject::invokeMethod(editor, "apply", Q_ARG(QVariant, changes));
      auto* selectAll = rootWindow->findChild<QQuickItem*>(QStringLiteral("bulkSelectAllButton"));
      if (library.selectionCount() != 0 || library.rowCount() != before - 2 || !selectAll || !selectAll->hasActiveFocus()) {
        fail(QStringLiteral("Bulk hiding: selected=%1 rows=%2 before=%3 focus=%4 message=%5")
            .arg(library.selectionCount()).arg(library.rowCount()).arg(before).arg(selectAll && selectAll->hasActiveFocus()).arg(library.bulkMessage())); return;
      }
      controller.backRequested();
      if (rootWindow->property("bulkOrganizationOpen").toBool()) { fail("Bulk editor cannot close"); return; }
      application.quit();
    });
  });
}

void driveArtworkEditor(Harness& harness, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  QWindow*& rootWindow = *app.rootWindow;
  QTemporaryDir& artworkFixture = harness.fixtures.artworkFixture;
  QTimer::singleShot(150, &application, [&application, rootWindow, &artworkFixture, &controller] {
    const auto fail = [&application](const QString& message) { qCritical() << message; application.exit(EXIT_FAILURE); };
    QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, 0));
    QMetaObject::invokeMethod(rootWindow, "editArtwork");
    auto* editor = rootWindow->findChild<QQuickItem*>(QStringLiteral("artworkEditor"));
    if (!editor || !editor->isVisible()) { fail("Artwork editor is not visible"); return; }
    QImage fixture(180, 90, QImage::Format_ARGB32);
    fixture.fill(QColor(60, 100, 160, 200));
    const QString path = artworkFixture.filePath(QStringLiteral("artwork.png"));
    if (!fixture.save(path)) { fail("Could not save artwork fixture"); return; }
    for (const QString& kind : {QStringLiteral("cover"), QStringLiteral("hero"), QStringLiteral("logo")}) {
      QMetaObject::invokeMethod(editor, "apply", Q_ARG(QVariant, kind), Q_ARG(QVariant, path));
    }
    const QVariantMap game = rootWindow->property("selectedGame").toMap();
    if (!game.value("customCover").toBool() || !game.value("customHero").toBool() || !game.value("customLogo").toBool()) {
      fail("Artwork editor did not persist all slots"); return;
    }
    const QString imageUrl = QUrl::fromLocalFile(path).toString();
    const auto waitForImageStatus = [](QQuickItem* image, int status) {
      if (!image) return false;
      QElapsedTimer timer;
      timer.start();
      while (image->property("status").toInt() != status && timer.elapsed() < 1500) {
        QEventLoop events;
        QTimer::singleShot(10, &events, &QEventLoop::quit);
        events.exec();
      }
      return image->property("status").toInt() == status;
    };
    for (const QString& kind : {QStringLiteral("cover"), QStringLiteral("hero"), QStringLiteral("logo")}) {
      auto* field = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPath_") + kind);
      auto* preview = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPathPreview_") + kind);
      if (!field || !preview) {
        if (kind == QStringLiteral("logo") && !rootWindow->property("couchMode").toBool())
          continue;
        fail("Artwork preview is missing beside its path field"); return;
      }
      field->setProperty("text", imageUrl);
      if (!waitForImageStatus(preview, 1) || !preview->isVisible()) {
        fail("Artwork path did not update its live preview"); return;
      }
    }
    auto* coverField = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPath_cover"));
    auto* coverPreview = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPathPreview_cover"));
    auto* coverFallback = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkEffectivePreview_cover"));
    auto* coverMessage = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPreviewMessage_cover"));
    if (!coverField || !coverPreview || !coverFallback || !coverMessage) {
      fail("Cover preview status controls are missing"); return;
    }
    coverField->setProperty("text", QUrl::fromLocalFile(
        artworkFixture.filePath(QStringLiteral("missing.png"))).toString());
    if (!waitForImageStatus(coverPreview, 3) || !coverFallback->isVisible() ||
        !coverMessage->property("text").toString().contains(QStringLiteral("CAN'T LOAD"),
                                                                Qt::CaseInsensitive)) {
      fail("An unreadable artwork path did not show its fallback and warning"); return;
    }
    coverField->setProperty("text", imageUrl);
    if (!waitForImageStatus(coverPreview, 1)) {
      fail("Artwork preview did not recover when a valid path was restored"); return;
    }
    QMetaObject::invokeMethod(editor, "reset", Q_ARG(QVariant, QStringLiteral("hero")));
    const QVariantMap reset = rootWindow->property("selectedGame").toMap();
    if (reset.value("customHero").toBool() || !reset.value("customCover").toBool() || !reset.value("customLogo").toBool()) {
      fail("Artwork reset changed other slots"); return;
    }
    QTimer::singleShot(100, &application, [&application, rootWindow, &controller, fail] {
      if (rootWindow->property("couchMode").toBool()) {
        const auto findVisual = [](auto&& self, QQuickItem* item) -> QQuickItem* {
          if (!item) return nullptr;
          if (item->objectName() == QStringLiteral("artworkPath_cover")) return item;
          for (auto* child : item->childItems()) if (auto* found = self(self, child)) return found;
          return nullptr;
        };
        auto* pathField = findVisual(findVisual, qobject_cast<QQuickWindow*>(rootWindow)->contentItem());
        if (!pathField) { fail("Artwork path field is missing"); return; }
        pathField->forceActiveFocus();
        controller.acceptRequested();
        if (!rootWindow->property("couchTextEntryOpen").toBool()) {
          fail("Artwork path cannot open controller text entry"); return;
        }
        QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
      }
      // The details stay open under the editor; X on the editor must not favorite that game.
      auto* artworkEditor = rootWindow->findChild<QQuickItem*>(QStringLiteral("artworkEditor"));
      if (!artworkEditor || !rootWindow->property("detailOpen").toBool()) {
        fail("Artwork editor is not open over the details"); return;
      }
      QMetaObject::invokeMethod(artworkEditor, "focusEditor");
      const bool favoriteBefore = rootWindow->property("selectedGame").toMap().value("favorite").toBool();
      controller.favoriteRequested();
      QCoreApplication::processEvents();
      if (rootWindow->property("selectedGame").toMap().value("favorite").toBool() != favoriteBefore ||
          !rootWindow->property("artworkEditorOpen").toBool()) {
        fail("Controller favorite changed the game behind the artwork editor"); return;
      }
      controller.backRequested();
      if (rootWindow->property("artworkEditorOpen").toBool()) { fail("Artwork editor cannot close"); return; }
      QCoreApplication::processEvents();
      controller.favoriteRequested();
      QCoreApplication::processEvents();
      if (rootWindow->property("selectedGame").toMap().value("favorite").toBool() == favoriteBefore) {
        fail("Controller favorite no longer works on the details"); return;
      }
      controller.favoriteRequested();
      application.quit();
    });
  });
}

void driveManualEditor(Harness&, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  ManualGameModel& manualGames = *app.manualGames;
  QWindow*& rootWindow = *app.rootWindow;
  QTimer::singleShot(150, &application, [&application, rootWindow, &manualGames, &controller] {
    const auto fail = [&application](const QString& message) { qCritical() << message; application.exit(EXIT_FAILURE); };
    if (!rootWindow) { fail("Manual editor has no window"); return; }
    QMetaObject::invokeMethod(rootWindow, "editManualGame", Q_ARG(QVariant, QString{}));
    auto* editor = rootWindow->findChild<QQuickItem*>(QStringLiteral("manualGameEditor"));
    auto* title = rootWindow->findChild<QQuickItem*>(QStringLiteral("manualTitleField"));
    if (!editor || !title || !editor->isVisible()) { fail("Manual editor is not visible"); return; }
    const QVariantMap draft{{"title", "Editor Test"},
                            {"executable", QStandardPaths::findExecutable(QStringLiteral("true"))},
                            {"directory", QDir::tempPath()},
                            {"arguments", QStringList{"two words", ""}}};
    QMetaObject::invokeMethod(editor, "loadDraft", Q_ARG(QVariant, draft));
    QTimer::singleShot(100, &application, [&application, rootWindow, editor, title, &manualGames, &controller, fail] {
      const QString fieldError = verifyEditorTextFields(qobject_cast<QQuickWindow*>(rootWindow), editor, controller);
      if (!fieldError.isEmpty()) { fail(fieldError); return; }
      if (rootWindow->property("couchMode").toBool()) {
        title->forceActiveFocus();
        controller.acceptRequested();
        if (!rootWindow->property("couchTextEntryOpen").toBool()) {
          fail("Manual title cannot open controller text entry"); return;
        }
        QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
      }
      QMetaObject::invokeMethod(editor, "save");
      if (manualGames.count() != 1 || rootWindow->property("manualEditorOpen").toBool()) {
        fail("Manual editor could not save: " + manualGames.lastError()); return;
      }
      const QString id = manualGames.data(manualGames.index(0), GameRoles::AppId).toString();
      if (manualGames.get(id).value("arguments").toStringList() != QStringList{"two words", ""}) {
        fail("Manual editor changed argument boundaries"); return;
      }
      QMetaObject::invokeMethod(rootWindow, "editManualGame", Q_ARG(QVariant, id));
      title->setProperty("text", "Edited Test");
      QMetaObject::invokeMethod(editor, "save");
      if (manualGames.count() != 1 || manualGames.get(id).value("title").toString() != "Edited Test") {
        fail("Editing a manual game changed its identity"); return;
      }
      application.quit();
    });
  });
}

} // namespace SelfTest
