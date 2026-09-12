#include "app/AppSettings.h"
#include "artwork/CoverImageProvider.h"
#include "theme/OmarchyTheme.h"
#include <QImage>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

class CoverArtworkTests : public QObject {
  Q_OBJECT
private slots:
  void cachedCoversDoNotLoopDuringStatusChanges() {
    QTemporaryDir dir;
    AppSettings preferences(dir.filePath("config"));
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme"));
    QQuickView view;
    view.engine()->addImageProvider("covers", new CoverImageProvider);
    view.rootContext()->setContextProperty("Theme", &theme);
    view.rootContext()->setContextProperty("Preferences", &preferences);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(200, 300);
    view.setSource(QUrl::fromLocalFile(OMAKADE_SOURCE_DIR "/qml/components/CoverArtwork.qml"));
    QVERIFY(view.status() == QQuickView::Ready);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::failOnWarning(QRegularExpression(".*Binding loop detected.*"));
    for (const QSize size : {QSize(200, 300), QSize(600, 200), QSize(200, 200)}) {
      const QString path = dir.filePath(QString::number(size.width()) + "x" +
                                        QString::number(size.height()) + ".png");
      QImage image(size, QImage::Format_RGB32);
      image.fill(Qt::red);
      QVERIFY(image.save(path));
      for (int repeat = 0; repeat < 3; ++repeat) {
        view.rootObject()->setProperty("source", QUrl::fromLocalFile(path));
        QTRY_COMPARE(view.rootObject()->property("status").toInt(), 1);
        QVERIFY(view.rootObject()->property("ready").toBool());
        QTest::qWait(30);
        QCOMPARE(view.rootObject()->property("wideArt").toBool(), size.width() == 600);
        view.rootObject()->setProperty("source", QUrl());
        QTRY_COMPARE(view.rootObject()->property("status").toInt(), 0);
        QVERIFY(!view.rootObject()->property("ready").toBool());
      }
    }
  }
};
QTEST_MAIN(CoverArtworkTests)
#include "CoverArtworkTests.moc"
