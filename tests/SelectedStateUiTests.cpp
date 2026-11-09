#include "app/AppSettings.h"
#include "theme/OmarchyTheme.h"
#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

// GlassButton's selected state has to read as selected on screen, including under keyboard
// focus, and reach screen readers as checked.
class SelectedStateUiTests : public QObject {
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
  static QAccessible::State state(QQuickItem* button) {
    QAccessibleInterface* face = QAccessible::queryAccessibleInterface(button);
    return face ? face->state() : QAccessible::State();
  }
  static double distance(const QColor& a, const QColor& b) {
    return std::abs(a.redF() - b.redF()) + std::abs(a.greenF() - b.greenF()) +
           std::abs(a.blueF() - b.blueF()) + std::abs(a.alphaF() - b.alphaF());
  }

private slots:
  void selectedStateIsVisibleAndAnnounced() {
    QTest::failOnWarning(
        QRegularExpression(QStringLiteral("Binding loop|TypeError|ReferenceError|Unable to")));
    QAccessible::setActive(true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString components = dir.filePath("components");
    QVERIFY(QDir().mkpath(components));
    for (const QString& file : {QStringLiteral("GlassButton.qml"), QStringLiteral("FocusRing.qml"),
                                QStringLiteral("UiMetrics.qml"), QStringLiteral("HeartIcon.qml")})
      QVERIFY(QFile::copy(QStringLiteral(OMAKADE_SOURCE_DIR "/qml/components/") + file,
                          components + "/" + file));
    QVERIFY(QFile::copy(
        QStringLiteral(OMAKADE_SOURCE_DIR "/tests/fixtures/selected-states/Buttons.qml"),
        components + "/Buttons.qml"));
    QFile qmldir(components + "/qmldir");
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write("singleton UiMetrics 1.0 UiMetrics.qml\nGlassButton 1.0 GlassButton.qml\n"
                 "FocusRing 1.0 FocusRing.qml\nHeartIcon 1.0 HeartIcon.qml\n");
    qmldir.close();

    AppSettings preferences(dir.filePath("config"));
    preferences.setReducedMotion(true);
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme-config"));
    QQuickView view;
    view.rootContext()->setContextProperty("Theme", &theme);
    view.rootContext()->setContextProperty("Preferences", &preferences);
    view.setSource(QUrl::fromLocalFile(components + "/Buttons.qml"));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QQuickItem* root = view.rootObject();
    QQuickItem* first = find(root, "firstTab");
    QQuickItem* second = find(root, "secondTab");
    QQuickItem* toggle = find(root, "toggle");
    QQuickItem* plain = find(root, "plain");
    QQuickItem* highlight = find(root, "highlight");
    QQuickItem* favorite = find(root, "favoriteToggle");
    QVERIFY(first && second && toggle && plain && highlight && favorite);

    // Tabs and toggles announce checked; ordinary buttons are not checkable at all.
    QVERIFY(state(first).checkable);
    QVERIFY(state(first).checked);
    QVERIFY(state(second).checkable);
    QVERIFY(!state(second).checked);
    QVERIFY(state(toggle).checkable);
    QVERIFY(!state(toggle).checked);
    QVERIFY(!state(plain).checkable);
    // A selected highlight on a button that is not selectable stays a plain button.
    QVERIFY(!state(highlight).checkable);
    QVERIFY(!state(highlight).checked);
    QVERIFY(!highlight->property("checked").toBool());

    // Selected has an accent fill and indicator, unlike its unselected neighbour.
    const QColor accent = theme.property("accent").value<QColor>();
    const auto accentDistance = [&](QQuickItem* button) {
      QColor color = fill(button);
      color.setAlphaF(accent.alphaF());
      return distance(color, accent);
    };
    QVERIFY(find(first, "selectedIndicator")->isVisible());
    QVERIFY(!find(second, "selectedIndicator")->isVisible());
    QVERIFY(accentDistance(first) < accentDistance(second));
    QVERIFY(fill(first).alphaF() > fill(second).alphaF() + 0.15);

    // Keyboard focus keeps the selected look rather than replacing it.
    first->forceActiveFocus();
    QTRY_VERIFY(first->hasActiveFocus());
    const QColor focusedSelected = fill(first);
    second->forceActiveFocus();
    QTRY_VERIFY(second->hasActiveFocus());
    const QColor focusedUnselected = fill(second);
    QVERIFY(distance(focusedSelected, focusedUnselected) > 0.2);
    QVERIFY(find(first, "selectedIndicator")->isVisible());

    // Clicking a tab that is already selected leaves it checked: the handler owns the state.
    const auto click = [&](QQuickItem* button) {
      const QPointF centre = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
      QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    };
    click(first);
    QVERIFY(first->property("checked").toBool());
    QVERIFY(state(first).checked);
    QVERIFY(!state(second).checked);

    click(second);
    QTRY_VERIFY(state(second).checked);
    QVERIFY(!state(first).checked);
    QVERIFY(!first->property("checked").toBool());

    click(toggle);
    QTRY_VERIFY(state(toggle).checked);
    QCOMPARE(toggle->property("text").toString(), QStringLiteral("ON"));
    QTest::keyClick(&view, Qt::Key_Space);
    QTRY_VERIFY(!state(toggle).checked);
    QCOMPARE(toggle->property("text").toString(), QStringLiteral("OFF"));

    // The favorite toggle reads as checked, and its drawn heart is filled with the accent
    // only while it is a favorite. A font heart looked the same both ways in the theme fonts.
    QQuickItem* heart = find(favorite, "favoriteHeart");
    QVERIFY(heart);
    const auto heartCentre = [&] {
      const QPointF centre = heart->mapToScene(QPointF(heart->width() / 2, heart->height() / 2));
      return view.grabWindow().pixelColor(centre.toPoint());
    };
    QVERIFY(state(favorite).checkable);
    QVERIFY(!state(favorite).checked);
    QVERIFY(!heart->property("filled").toBool());
    QVERIFY(distance(heartCentre(), accent) > 0.3);
    click(favorite);
    QTRY_VERIFY(state(favorite).checked);
    QCOMPARE(favorite->property("text").toString(), QStringLiteral("FAVORITED"));
    QVERIFY(heart->property("filled").toBool());
    QVERIFY(find(favorite, "selectedIndicator")->isVisible());
    QTRY_VERIFY(distance(heartCentre(), accent) < 0.1);
    click(favorite);
    QTRY_VERIFY(!state(favorite).checked);
    QVERIFY(!heart->property("filled").toBool());

    QVERIFY(view.grabWindow().save("selected-states.png"));
  }
};
QTEST_MAIN(SelectedStateUiTests)
#include "SelectedStateUiTests.moc"
