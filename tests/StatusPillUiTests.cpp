#include "app/AppSettings.h"
#include "metadata/ProtonDbService.h"
#include "theme/OmarchyTheme.h"
#include <QDir>
#include <QFile>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

class StatusPillUiTests : public QObject {
  Q_OBJECT
private slots:
  void pillsAreReadableAndClearOfTheFavoriteBadge() {
    QTest::failOnWarning(
        QRegularExpression(QStringLiteral("Binding loop|TypeError|ReferenceError")));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Catppuccin Latte, where dark theme text is unreadable on the blue accent fill.
    const QString themeDir = dir.filePath("state/omarchy/current/theme");
    QVERIFY(QDir().mkpath(themeDir));
    QFile colors(themeDir + "/colors.toml");
    QVERIFY(colors.open(QIODevice::WriteOnly));
    colors.write("mode = \"light\"\naccent = \"#1e66f5\"\n"
                 "background = \"#eff1f5\"\ndark_background = \"#e3e4e8\"\n"
                 "darker_background = \"#d7d8dc\"\nlighter_background = \"#dce0e8\"\n"
                 "foreground = \"#4c4f69\"\ndark_foreground = \"#9ca0b0\"\n"
                 "light_foreground = \"#5c5f77\"\nbright_foreground = \"#4c4f69\"\n"
                 "yellow = \"#df8e1d\"\ngreen = \"#40a02b\"\n");
    colors.close();
    OmarchyTheme theme(dir.filePath("state"), dir.filePath("theme-config"));
    QVERIFY(theme.omarchyAvailable());
    AppSettings preferences(dir.filePath("config"));
    ProtonDbService protonDb(dir.filePath("protondb-cache"));
    QQuickView view;
    view.rootContext()->setContextProperty("Theme", &theme);
    view.rootContext()->setContextProperty("Preferences", &preferences);
    view.rootContext()->setContextProperty("ProtonDB", &protonDb);
    view.setSource(QUrl::fromLocalFile(
        QStringLiteral(OMAKADE_SOURCE_DIR "/tests/fixtures/status-pills/Cards.qml")));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QList<QQuickItem*> pills;
    const auto collect = [&](auto&& self, QQuickItem* item) -> void {
      if (item->objectName() == "statusPill")
        pills.append(item);
      for (auto* child : item->childItems())
        self(self, child);
    };
    collect(collect, view.rootObject());
    QCOMPARE(pills.size(), 10);

    const auto child = [](QQuickItem* parent, const QString& name) -> QQuickItem* {
      for (auto* item : parent->childItems()) {
        if (item->objectName() == name)
          return item;
      }
      return nullptr;
    };
    for (auto* pill : pills) {
      QVERIFY(pill->isVisible());
      auto* text = child(pill, QStringLiteral("statusPillText"));
      auto* cover = pill->parentItem();
      auto* badge = child(cover, QStringLiteral("favoriteBadge"));
      QVERIFY(text && badge);
      const QString label = text->property("text").toString() + QStringLiteral(" at ") +
                            QString::number(cover->width());

      const QColor fill = pill->property("color").value<QColor>();
      const QColor ink = text->property("color").value<QColor>();
      QVERIFY2(fill.alpha() == 255, qPrintable(label));
      QVERIFY2(OmarchyTheme::contrastRatio(ink, fill) >= 4.5, qPrintable(label));

      // The label stays inside the pill and the pill inside the cover.
      QVERIFY2(text->property("contentWidth").toReal() <= pill->width(), qPrintable(label));
      const qreal pillRight = pill->x() + pill->width();
      QVERIFY2(pillRight <= cover->width() - 9, qPrintable(label));
      // The heart sits in a Loader that only favorites activate, so other cards never paint it.
      QQuickItem* heart = nullptr;
      for (auto* loader : badge->childItems()) {
        if (!heart)
          heart = child(loader, QStringLiteral("favoriteHeart"));
      }
      QVERIFY2(badge->isVisible() == (heart != nullptr), qPrintable(label));
      if (badge->isVisible()) {
        QVERIFY2(pillRight < badge->x(), qPrintable(label));
        // The favorite badge is opaque and its heart is a filled shape that stands out on it.
        QVERIFY2(heart->property("filled").toBool(), qPrintable(label));
        const QColor badgeFill = badge->property("color").value<QColor>();
        QVERIFY2(badgeFill.alpha() == 255, qPrintable(label));
        QVERIFY2(OmarchyTheme::contrastRatio(heart->property("color").value<QColor>(), badgeFill) >= 4.5,
                 qPrintable(label));
      }

      // Wide cards keep the full size label; only the narrow favorite cards shrink it.
      if (cover->width() >= 200) {
        QVERIFY2(!pill->property("tight").toBool(), qPrintable(label));
        QVERIFY2(!text->property("truncated").toBool(), qPrintable(label));
      }
    }
    QVERIFY(view.grabWindow().save("status-pills.png"));
  }
};
QTEST_MAIN(StatusPillUiTests)
#include "StatusPillUiTests.moc"
