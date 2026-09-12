#include "app/AppSettings.h"
#include "metadata/ProtonDbService.h"
#include "theme/OmarchyTheme.h"
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

class ProtonDbUiTests : public QObject {
  Q_OBJECT
private slots:
  void cardsKeepTwoCaptionRowsWithProtonDbEnabled() {
    QTemporaryDir dir;
    QFile file(dir.filePath("cache"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        QJsonDocument(
            QJsonObject{{"620", QJsonObject{{"tier", "platinum"},
                                            {"total", 339},
                                            {"fetched", QDateTime::currentSecsSinceEpoch()}}}})
            .toJson());
    file.close();
    ProtonDbService service(file.fileName());
    service.setEnabled(true);
    AppSettings preferences(dir.filePath("config"));
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme-config"));
    QQuickView view;
    view.rootContext()->setContextProperty("Theme", &theme);
    view.rootContext()->setContextProperty("Preferences", &preferences);
    view.rootContext()->setContextProperty("ProtonDB", &service);
    view.setSource(QUrl::fromLocalFile(
        QStringLiteral(OMAKADE_SOURCE_DIR "/tests/fixtures/protondb/Cards.qml")));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QList<QQuickItem*> captions;
    const auto collect = [&](auto&& self, QQuickItem* item) -> void {
      if (item->objectName() == "cardCaption")
        captions.append(item);
      for (auto* child : item->childItems())
        self(self, child);
    };
    collect(collect, view.rootObject());
    QCOMPARE(captions.size(), 3);
    for (auto* caption : captions) {
      QCOMPARE(caption->childItems().size(), 2);
      auto* card = caption->parentItem();
      const auto point = caption->mapToItem(card, QPointF(0, 0));
      QVERIFY(point.y() + caption->height() <= card->height());
      QVERIFY(point.x() + caption->width() <= card->width());
    }
    QTest::qWait(100);
    QVERIFY(view.grabWindow().save("protondb-cards.png"));
    service.setEnabled(false);
    for (auto* caption : captions)
      QCOMPARE(caption->childItems().size(), 2);
  }
};
QTEST_MAIN(ProtonDbUiTests)
#include "ProtonDbUiTests.moc"
