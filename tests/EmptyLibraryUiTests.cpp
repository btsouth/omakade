#include "app/AppSettings.h"
#include "theme/OmarchyTheme.h"
#include <QDir>
#include <QFile>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// An empty library, as on a first run, offers a way to set up sources and read the guide
// next to Rescan, starts keyboard and controller focus on setup, and wraps its text.
class EmptyLibraryUiTests : public QObject {
  Q_OBJECT

  static QQuickItem* find(QQuickItem* item, const QString& name) {
    if (item->objectName() == name)
      return item;
    for (auto* child : item->childItems()) {
      if (auto* found = find(child, name))
        return found;
    }
    return nullptr;
  }

private slots:
  void emptyLibraryOffersSetup() {
    QTest::failOnWarning(
        QRegularExpression(QStringLiteral("Binding loop|TypeError|ReferenceError|Unable to")));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString components = dir.filePath("components");
    QVERIFY(QDir().mkpath(components));
    QByteArray types;
    const QDir source(QStringLiteral(OMAKADE_SOURCE_DIR "/qml/components"));
    for (const QFileInfo& file : source.entryInfoList({"*.qml", "*.js"}, QDir::Files)) {
      QVERIFY(QFile::copy(file.filePath(), components + "/" + file.fileName()));
      if (file.suffix() != QStringLiteral("qml"))
        continue;
      const QByteArray name = file.completeBaseName().toUtf8();
      const bool singleton = name == "UiMetrics" || name == "TextEntry";
      types += (singleton ? "singleton " : "") + name + " 1.0 " + file.fileName().toUtf8() + "\n";
    }
    QVERIFY(QFile::copy(
        QStringLiteral(OMAKADE_SOURCE_DIR "/tests/fixtures/empty-library/EmptyLibrary.qml"),
        components + "/EmptyLibrary.qml"));
    QFile qmldir(components + "/qmldir");
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write(types);
    qmldir.close();

    AppSettings preferences(dir.filePath("config"));
    preferences.setReducedMotion(true);
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme-config"));
    QQuickView view;
    view.rootContext()->setContextProperty("Theme", &theme);
    view.rootContext()->setContextProperty("Preferences", &preferences);
    view.setSource(QUrl::fromLocalFile(components + "/EmptyLibrary.qml"));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&view));

    QQuickItem* root = view.rootObject();
    QQuickItem* library = find(root, "libraryView");
    QQuickItem* setup = find(root, "emptySetupButton");
    QQuickItem* rescan = find(root, "emptyRescanButton");
    QQuickItem* guide = find(root, "emptyGuideButton");
    QQuickItem* clear = find(root, "emptyClearButton");
    QQuickItem* message = find(root, "emptyMessage");
    QVERIFY(library && setup && rescan && guide && clear && message);

    // Setup, Rescan and the guide sit side by side, with setup first and taking focus.
    QVERIFY(setup->isVisible());
    QVERIFY(rescan->isVisible());
    QVERIFY(guide->isVisible());
    QVERIFY(!clear->isVisible());
    QVERIFY(setup->property("primary").toBool());
    QCOMPARE(library->property("focusTarget").value<QQuickItem*>(), setup);
    QVERIFY(QMetaObject::invokeMethod(library, "focusGrid"));
    QTRY_VERIFY(setup->hasActiveFocus());

    // The long explanation wraps inside the view instead of running off its edges.
    QVERIFY(message->width() <= 640);
    QVERIFY(message->property("lineCount").toInt() > 1);

    QSignalSpy setupSpy(library, SIGNAL(setupRequested()));
    QSignalSpy guideSpy(library, SIGNAL(guideRequested()));
    QSignalSpy rescanSpy(library, SIGNAL(refreshRequested()));
    QTest::keyClick(&view, Qt::Key_Space);
    QTRY_COMPARE(setupSpy.count(), 1);
    const auto click = [&](QQuickItem* button) {
      const QPointF centre = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
      QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    };
    click(guide);
    QTRY_COMPARE(guideSpy.count(), 1);
    click(rescan);
    QTRY_COMPARE(rescanSpy.count(), 1);
    QCOMPARE(setupSpy.count(), 1);

    // With filters on, clearing them is the only action; setup would not help.
    library->setProperty("filtersActive", true);
    QVERIFY(clear->isVisible());
    QVERIFY(!setup->isVisible());
    QVERIFY(!guide->isVisible());
    QVERIFY(!rescan->isVisible());
    QCOMPARE(library->property("focusTarget").value<QQuickItem*>(), clear);

    // A view that is empty for another reason, such as Favorites, keeps only Rescan.
    library->setProperty("filtersActive", false);
    library->setProperty("setupAvailable", false);
    QVERIFY(rescan->isVisible());
    QVERIFY(!setup->isVisible());
    QVERIFY(!guide->isVisible());
    QCOMPARE(library->property("focusTarget").value<QQuickItem*>(), rescan);

    // Nothing to press while a scan is still running.
    library->setProperty("setupAvailable", true);
    library->setProperty("scanning", true);
    QVERIFY(!setup->isVisible());
    QVERIFY(!rescan->isVisible());
    QVERIFY(!guide->isVisible());
  }
};
QTEST_MAIN(EmptyLibraryUiTests)
#include "EmptyLibraryUiTests.moc"
