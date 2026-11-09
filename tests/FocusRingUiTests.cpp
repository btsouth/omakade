#include "app/AppSettings.h"
#include "artwork/CoverImageProvider.h"
#include "metadata/ProtonDbService.h"
#include "theme/OmarchyTheme.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

// Keyboard and controller focus has its own ring: thick, 3:1 against the page, shown only on
// the item that has focus, and unlike both hover and the library's retained current card.
class FocusRingUiTests : public QObject {
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
  static QColor fill(QQuickItem* button) {
    auto* background = button->property("background").value<QQuickItem*>();
    return background ? background->property("color").value<QColor>() : QColor();
  }
  static QObject* border(QQuickItem* item) {
    return item->property("border").value<QObject*>();
  }

private slots:
  void focusRingIsDistinctFromHoverAndTheCurrentCard() {
    QTest::failOnWarning(
        QRegularExpression(QStringLiteral("Binding loop|TypeError|ReferenceError|Unable to")));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // The components with a qmldir, so the UiMetrics and TextEntry singletons resolve.
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
    QVERIFY(QFile::copy(QStringLiteral(OMAKADE_SOURCE_DIR "/tests/fixtures/focus-ring/Controls.qml"),
                        components + "/Controls.qml"));
    QFile qmldir(components + "/qmldir");
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write(types);
    qmldir.close();
    AppSettings preferences(dir.filePath("config"));
    preferences.setReducedMotion(true);
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme-config"));
    ProtonDbService protonDb(dir.filePath("protondb-cache"));
    // Real artwork for the current card, so its marker is checked against art, not the gradient.
    QImage art(200, 300, QImage::Format_RGB32);
    art.fill(Qt::red);
    const QString coverFile = dir.filePath("cover.png");
    QVERIFY(art.save(coverFile));
    QQuickView view;
    view.engine()->addImageProvider(QStringLiteral("covers"), new CoverImageProvider);
    view.rootContext()->setContextProperty("fixtureCover", coverFile);
    view.rootContext()->setContextProperty("Theme", &theme);
    view.rootContext()->setContextProperty("Preferences", &preferences);
    view.rootContext()->setContextProperty("ProtonDB", &protonDb);
    view.setSource(QUrl::fromLocalFile(components + "/Controls.qml"));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&view));

    QQuickItem* root = view.rootObject();
    QQuickItem* button = find(root, "button");
    QQuickItem* other = find(root, "other");
    QQuickItem* field = find(root, "field");
    QQuickItem* currentCard = find(root, "currentCard");
    QQuickItem* checkBox = find(root, "checkBox");
    QVERIFY(button && other && field && currentCard && checkBox);
    QQuickItem* buttonRing = find(button, "focusRing");
    QQuickItem* otherRing = find(other, "focusRing");
    QQuickItem* fieldRing = find(field, "focusRing");
    QQuickItem* cardRing = find(currentCard, "focusRing");
    QQuickItem* checkRing = find(checkBox, "focusRing");
    QQuickItem* marker = find(currentCard, "currentMarker");
    QVERIFY(buttonRing && otherRing && fieldRing && cardRing && checkRing && marker);
    QVERIFY(currentCard->property("current").toBool());
    QVERIFY(checkBox->property("checked").toBool());

    // Wait for the current card's artwork to paint over its cover.
    const auto sceneAt = [](QQuickItem* item, qreal x, qreal y) {
      const QPointF point = item->mapToScene(QPointF(x, y));
      return QPoint(int(std::floor(point.x())), int(std::floor(point.y())));
    };
    QTRY_COMPARE(view.grabWindow().pixelColor(sceneAt(marker, marker->width() / 2,
                                                      marker->height() / 2)),
                 QColor(Qt::red));

    // Nothing has focus yet, so no ring shows, not even on the current card.
    QVERIFY(!buttonRing->isVisible());
    QVERIFY(!fieldRing->isVisible());
    QVERIFY(!cardRing->isVisible());
    const QColor idle = fill(button);

    // Hover changes the fill and never draws the ring.
    const auto centre = [](QQuickItem* item) {
      return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    };
    QTest::mouseMove(&view, centre(button) - QPoint(4, 0));
    QTest::mouseMove(&view, centre(button));
    QTRY_VERIFY(button->property("hovered").toBool());
    QVERIFY(fill(button) != idle);
    QVERIFY(!buttonRing->isVisible());
    QTest::mouseMove(&view, QPoint(view.width() - 4, view.height() - 4));
    QTRY_VERIFY(!button->property("hovered").toBool());

    // Focus draws a ring of at least 3 px at 3:1 against the page, and leaves the fill alone so
    // it cannot be mistaken for hover. It stays inside the button's edge, where a clipping list
    // cannot cut it off.
    const QColor page = root->property("color").value<QColor>();
    button->forceActiveFocus();
    QTRY_VERIFY(button->hasActiveFocus());
    QVERIFY(buttonRing->isVisible());
    QVERIFY(border(buttonRing)->property("width").toReal() >= 3);
    const QColor ring = border(buttonRing)->property("color").value<QColor>();
    QCOMPARE(ring, theme.focusRing());
    QVERIFY(OmarchyTheme::contrastRatio(ring, page) >= 3.0);
    QCOMPARE(buttonRing->width(), button->width());
    QCOMPARE(buttonRing->height(), button->height());
    QCOMPARE(fill(button), idle);
    QVERIFY(!otherRing->isVisible());

    // The search field takes focus while the current card stays current: only the field rings.
    field->forceActiveFocus();
    QTRY_VERIFY(field->hasActiveFocus());
    QVERIFY(fieldRing->isVisible());
    QVERIFY(border(fieldRing)->property("width").toReal() >= 3);
    QVERIFY(!buttonRing->isVisible());
    QVERIFY(currentCard->property("current").toBool());
    QVERIFY(!cardRing->isVisible());
    // The current card keeps its quiet marker, drawn outside the cover so the artwork, which
    // covers the cover's own border, cannot hide it.
    QVERIFY(marker->isVisible());
    {
      const QImage frame = view.grabWindow();
      const QColor edge = frame.pixelColor(sceneAt(marker, 1, marker->height() / 2));
      QVERIFY2(edge != page && edge != QColor(Qt::red), qPrintable(edge.name()));
      QVERIFY(OmarchyTheme::contrastRatio(edge, page) >= 1.5);
    }

    // A checked box is filled with the accent, which is often the ring color too, so its ring
    // lies outside the box rather than on the fill.
    checkBox->forceActiveFocus();
    QTRY_VERIFY(checkBox->hasActiveFocus());
    QVERIFY(checkRing->isVisible());
    QQuickItem* indicator = checkBox->property("indicator").value<QQuickItem*>();
    QVERIFY(indicator);
    const qreal checkThickness = border(checkRing)->property("width").toReal();
    QVERIFY(checkThickness >= 3);
    QVERIFY(checkRing->width() - 2 * checkThickness >= indicator->width());
    QVERIFY(checkRing->height() - 2 * checkThickness >= indicator->height());
    QVERIFY(!fieldRing->isVisible());

    // Focus on the card itself brings its ring, outside the cover so artwork cannot hide it.
    currentCard->forceActiveFocus();
    QTRY_VERIFY(currentCard->hasActiveFocus());
    QVERIFY(cardRing->isVisible());
    QVERIFY(border(cardRing)->property("width").toReal() >= 3);
    QVERIFY(cardRing->width() > currentCard->width());
    QVERIFY(!marker->isVisible());
    QVERIFY(!fieldRing->isVisible());
    QVERIFY(!checkRing->isVisible());
  }
};
QTEST_MAIN(FocusRingUiTests)
#include "FocusRingUiTests.moc"
