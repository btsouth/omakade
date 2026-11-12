#include "app/AppSettings.h"
#include "metadata/ProtonDbService.h"
#include "theme/OmarchyTheme.h"
#include <QAccessible>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

// GlassButton's selected state has to read as selected on screen, including under keyboard
// focus, and reach screen readers as checked. Game cards tell screen readers the state they
// show and can be opened from them.
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
  // Copies GlassButton and a fixture from tests/fixtures/selected-states into one folder,
  // so the fixture resolves GlassButton from beside it, and shows it.
  static void showButtonFixture(QQuickView& view, const QTemporaryDir& dir, const QString& fixture,
                                OmarchyTheme& theme, AppSettings& preferences) {
    const QString components = dir.filePath("components");
    QVERIFY(QDir().mkpath(components));
    for (const QString& file : {QStringLiteral("GlassButton.qml"), QStringLiteral("FocusRing.qml"),
                                QStringLiteral("UiMetrics.qml"), QStringLiteral("HeartIcon.qml")})
      QVERIFY(QFile::copy(QStringLiteral(OMAKADE_SOURCE_DIR "/qml/components/") + file,
                          components + "/" + file));
    QVERIFY(
        QFile::copy(QStringLiteral(OMAKADE_SOURCE_DIR "/tests/fixtures/selected-states/") + fixture,
                    components + "/" + fixture));
    QFile qmldir(components + "/qmldir");
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write("singleton UiMetrics 1.0 UiMetrics.qml\nGlassButton 1.0 GlassButton.qml\n"
                 "FocusRing 1.0 FocusRing.qml\nHeartIcon 1.0 HeartIcon.qml\n");
    qmldir.close();

    view.rootContext()->setContextProperty("Theme", &theme);
    view.rootContext()->setContextProperty("Preferences", &preferences);
    view.setSource(QUrl::fromLocalFile(components + "/" + fixture));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
  }
  static QRectF sceneRect(QQuickItem* item) {
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
  }
  static QColor opaque(QColor color) {
    color.setAlphaF(1);
    return color;
  }

private slots:
  void selectedStateIsVisibleAndAnnounced() {
    QTest::failOnWarning(
        QRegularExpression(QStringLiteral("Binding loop|TypeError|ReferenceError|Unable to")));
    QAccessible::setActive(true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AppSettings preferences(dir.filePath("config"));
    preferences.setReducedMotion(true);
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme-config"));
    QQuickView view;
    showButtonFixture(view, dir, QStringLiteral("Buttons.qml"), theme, preferences);
    if (QTest::currentTestFailed())
      return;

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

  // Labels sit in the middle of a button wider than they are, the screen's main action is a
  // solid fill with readable text that outranks a selected toggle, and destructive confirms
  // get their own solid red.
  void buttonLabelsCenterAndPrimaryLeads() {
    QTest::failOnWarning(
        QRegularExpression(QStringLiteral("Binding loop|TypeError|ReferenceError|Unable to")));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AppSettings preferences(dir.filePath("config"));
    preferences.setReducedMotion(true);
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme-config"));
    QQuickView view;
    showButtonFixture(view, dir, QStringLiteral("Hierarchy.qml"), theme, preferences);
    if (QTest::currentTestFailed())
      return;

    QQuickItem* root = view.rootObject();
    QQuickItem* wide = find(root, "wide");
    QQuickItem* leftAligned = find(root, "leftAligned");
    QQuickItem* primary = find(root, "primary");
    QQuickItem* primarySelected = find(root, "primarySelected");
    QQuickItem* selected = find(root, "selected");
    QQuickItem* secondary = find(root, "secondary");
    QQuickItem* destructive = find(root, "destructive");
    QQuickItem* narrow = find(root, "narrow");
    QVERIFY(wide && leftAligned && primary && primarySelected && selected && secondary &&
            destructive && narrow);

    // The icon and label together are centred in the button, not started at its left edge.
    const QRectF wideButton = sceneRect(wide);
    const QRectF wideContent = sceneRect(find(wide, "buttonContent"));
    QVERIFY(wideContent.width() < wideButton.width() / 2);
    QVERIFY2(
        std::abs(wideContent.center().x() - wideButton.center().x()) <= 1,
        qPrintable(QString("%1 vs %2").arg(wideContent.center().x()).arg(wideButton.center().x())));
    // Menu rows can still keep a left label column.
    const QRectF leftContent = sceneRect(find(leftAligned, "buttonContent"));
    QVERIFY(std::abs(leftContent.left() - sceneRect(leftAligned).left() -
                     leftAligned->property("leftPadding").toReal()) <= 1);
    // A label wider than its button is elided inside it rather than spilling out.
    QQuickItem* narrowLabel = find(narrow, "buttonLabel");
    QVERIFY(narrowLabel->property("truncated").toBool());
    QVERIFY(sceneRect(narrowLabel).right() <= sceneRect(narrow).right());
    // A button sized to its own label shows all of it at a fractional scale.
    QQuickItem* fittedChips = find(root, "fittedChips");
    QVERIFY(fittedChips);
    int fitted = 0;
    for (QQuickItem* chip : fittedChips->childItems()) {
      QQuickItem* label = find(chip, "buttonLabel");
      if (!label)
        continue;
      ++fitted;
      QVERIFY2(!label->property("truncated").toBool(),
               qPrintable(chip->property("text").toString()));
    }
    QCOMPARE(fitted, 4);

    // Primary is the solid accent with text that reads on it at WCAG AA.
    const QColor accent = theme.property("accent").value<QColor>();
    const QColor red = theme.property("destructive").value<QColor>();
    const auto labelColor = [&](QQuickItem* button) {
      return find(button, "buttonLabel")->property("color").value<QColor>();
    };
    QCOMPARE(fill(primary).alpha(), 255);
    QVERIFY(distance(fill(primary), opaque(accent)) < 0.02);
    QVERIFY(OmarchyTheme::contrastRatio(labelColor(primary), fill(primary)) >= 4.5);
    // It outranks the selected toggle and the plain secondary, and stays distinct from both.
    QVERIFY(fill(primary).alphaF() > fill(selected).alphaF() + 0.5);
    QVERIFY(fill(selected).alphaF() > fill(secondary).alphaF());
    QVERIFY(find(selected, "selectedIndicator")->isVisible());
    QVERIFY(!find(primary, "selectedIndicator")->isVisible());
    // Selecting a primary button keeps the primary look rather than the toggle tint.
    QVERIFY(!find(primarySelected, "selectedIndicator")->isVisible());
    QCOMPARE(fill(primarySelected), fill(primary));

    // A destructive confirm is solid red, readable, and not the primary accent.
    QCOMPARE(fill(destructive).alpha(), 255);
    QVERIFY(distance(fill(destructive), opaque(red)) < 0.02);
    QVERIFY(OmarchyTheme::contrastRatio(labelColor(destructive), fill(destructive)) >= 4.5);
    QVERIFY(distance(fill(destructive), fill(primary)) > 0.3);

    // The focus ring on a solid button uses the label colour, so it shows on the fill.
    primary->forceActiveFocus();
    QTRY_VERIFY(primary->hasActiveFocus());
    QQuickItem* ring = find(primary, "focusRing");
    QVERIFY(ring && ring->isVisible());
    const QColor ringColor =
        ring->property("border").value<QObject*>()->property("color").value<QColor>();
    QVERIFY(OmarchyTheme::contrastRatio(ringColor, fill(primary)) >= 3.0);

    // Disabled primary buttons fade instead of keeping the solid call to action.
    primary->setProperty("enabled", false);
    QVERIFY(fill(primary).alphaF() < 0.5);
    QVERIFY(view.grabWindow().save("button-hierarchy.png"));
  }

  void gameCardsAnnounceTheirState() {
    QTest::failOnWarning(
        QRegularExpression(QStringLiteral("Binding loop|TypeError|ReferenceError|Unable to")));
    QAccessible::setActive(true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AppSettings preferences(dir.filePath("config"));
    preferences.setReducedMotion(true);
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme-config"));
    ProtonDbService protonDb(dir.filePath("protondb-cache"));
    QQuickView view;
    view.rootContext()->setContextProperty("Theme", &theme);
    view.rootContext()->setContextProperty("Preferences", &preferences);
    view.rootContext()->setContextProperty("ProtonDB", &protonDb);
    view.setSource(QUrl::fromLocalFile(
        QStringLiteral(OMAKADE_SOURCE_DIR "/tests/fixtures/selected-states/GameCards.qml")));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QQuickItem* root = view.rootObject();
    QQuickItem* favorite = find(root, "favoriteCard");
    QQuickItem* plain = find(root, "plainCard");
    QVERIFY(favorite && plain);
    QAccessibleInterface* face = QAccessible::queryAccessibleInterface(favorite);
    QVERIFY(face);
    QCOMPARE(face->role(), QAccessible::ListItem);
    QCOMPARE(face->text(QAccessible::Name), QStringLiteral("Portal 2"));
    // The heart, the status pill and the dimmed cover all reach the screen reader.
    QCOMPARE(face->text(QAccessible::Description),
             QStringLiteral("Steam, 12h played, rated 91 out of 100, favorite, status playing, "
                            "not installed"));
    QAccessibleInterface* plainFace = QAccessible::queryAccessibleInterface(plain);
    QVERIFY(plainFace);
    QCOMPARE(plainFace->text(QAccessible::Description), QStringLiteral("Heroic, 0h played"));

    // A screen reader can open the game, as Enter and a click do.
    QAccessibleActionInterface* actions = face->actionInterface();
    QVERIFY(actions);
    QVERIFY(actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    actions->doAction(QAccessibleActionInterface::pressAction());
    QTRY_COMPARE(root->property("activations").toInt(), 1);

    // A hidden achievement keeps its name and description secret until it is unlocked.
    QQuickItem* locked = find(root, "achievementCard0");
    QQuickItem* unlocked = find(root, "achievementCard1");
    QVERIFY(locked && unlocked);
    QAccessibleInterface* lockedFace = QAccessible::queryAccessibleInterface(locked);
    QVERIFY(lockedFace);
    QCOMPARE(lockedFace->role(), QAccessible::ListItem);
    QCOMPARE(lockedFace->text(QAccessible::Name), QStringLiteral("Hidden achievement"));
    QCOMPARE(lockedFace->text(QAccessible::Description),
             QStringLiteral("Unlock to reveal details, locked, 3.3% of players"));
    QAccessibleInterface* unlockedFace = QAccessible::queryAccessibleInterface(unlocked);
    QVERIFY(unlockedFace);
    QCOMPARE(unlockedFace->text(QAccessible::Name), QStringLiteral("Secret Ending"));
    const QString unlockedOn = QLocale().toString(QDateTime::fromSecsSinceEpoch(1794139200).date(),
                                                  QStringLiteral("MMM d, yyyy"));
    QCOMPARE(unlockedFace->text(QAccessible::Description),
             QStringLiteral("Beat the final boss, unlocked %1, 3.3% of players").arg(unlockedOn));
  }
};
QTEST_MAIN(SelectedStateUiTests)
#include "SelectedStateUiTests.moc"
