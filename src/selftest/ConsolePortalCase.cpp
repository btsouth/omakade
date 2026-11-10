#include "selftest/SelfTest.h"

#include "library/LibraryFilterModel.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QList>
#include <QPointF>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>

#include <cstdlib>
#include <functional>
#include <memory>

namespace SelfTest {

namespace {

// Opens the Super Nintendo portal in the desktop grid and checks that every card the
// user can see belongs to the console, that no card from the previous view is left
// behind, and that leaving the console removes the cartridges again. The proxy is
// covered by core tests; this guards the QML view, which recycles delegates.
void runConsolePortalTest(QQuickWindow* window, QGuiApplication* application) {
  const auto fail = [application](const QString& message) {
    qCritical().noquote() << message;
    application->exit(EXIT_FAILURE);
  };
  auto* library = qobject_cast<QAbstractItemModel*>(
      qmlContext(window)->contextProperty(QStringLiteral("Library")).value<QObject*>());
  auto* grid = window->findChild<QQuickItem*>(QStringLiteral("libraryGrid"));
  if (library == nullptr || grid == nullptr) {
    fail(QStringLiteral("Console portal test could not find the library grid"));
    return;
  }
  struct VisibleCard {
    QString title;
    QString source;
    QPointF position;
  };
  const auto visibleCards = [grid] {
    QVector<VisibleCard> cards;
    auto* content = grid->property("contentItem").value<QQuickItem*>();
    if (content == nullptr) {
      return cards;
    }
    const qreal top = grid->property("contentY").toReal();
    const qreal bottom = top + grid->height();
    for (QQuickItem* child : content->childItems()) {
      if (!child->property("appId").isValid() || !child->isVisible() || child->opacity() <= 0 ||
          child->y() + child->height() <= top || child->y() >= bottom) {
        continue;
      }
      cards.append({child->property("title").toString(), child->property("source").toString(),
                    child->position()});
    }
    return cards;
  };
  const auto portalRow = [library] {
    int row = -1;
    QMetaObject::invokeMethod(library, "indexOf", Q_RETURN_ARG(int, row),
                              Q_ARG(QString, QStringLiteral("RetroArch")), Q_ARG(QString, QString{}),
                              Q_ARG(QString, QStringLiteral("portal:snes")));
    return row;
  };
  const auto checkConsoleView = [visibleCards, library, grid, fail](const QString& phase) {
    const QVector<VisibleCard> cards = visibleCards();
    if (cards.isEmpty()) {
      qWarning() << "Grid diagnostics" << grid->property("count") << grid->property("contentY")
                 << grid->property("originY") << grid->property("contentHeight") << grid->height();
      auto* content = grid->property("contentItem").value<QQuickItem*>();
      if (content) for (auto* child : content->childItems())
        if (child->property("appId").isValid()) qWarning() << child->property("title") << child->y() << child->isVisible();
      fail(QStringLiteral("%1: no cards are visible inside the console").arg(phase));
      return false;
    }
    QSet<QString> positions;
    for (const VisibleCard& card : cards) {
      if (card.source != QStringLiteral("RetroArch") ||
          !card.title.startsWith(QStringLiteral("SNES Cart"))) {
        fail(QStringLiteral("%1: '%2' from %3 is visible under Super Nintendo")
                 .arg(phase, card.title, card.source));
        return false;
      }
      const QString key = QStringLiteral("%1,%2").arg(card.position.x()).arg(card.position.y());
      if (positions.contains(key)) {
        fail(QStringLiteral("%1: two cards overlap at %2 (stale delegate)").arg(phase, key));
        return false;
      }
      positions.insert(key);
    }
    if (library->property("consoleFilter").toString() != QStringLiteral("snes")) {
      fail(QStringLiteral("%1: the console filter is not set").arg(phase));
      return false;
    }
    return true;
  };
  const int firstPortal = portalRow();
  if (firstPortal < 0) {
    fail(QStringLiteral("Console portal test could not find the Super Nintendo portal"));
    return;
  }
  QElapsedTimer openTimer;
  openTimer.start();
  QMetaObject::invokeMethod(window, "openGame", Q_ARG(QVariant, QVariant(firstPortal)));
  const qint64 openMs = openTimer.elapsed();
  qInfo().noquote() << QStringLiteral("Opening the console took %1 ms").arg(openMs);
  QTimer::singleShot(400, window, [=] {
    if (openMs > 1000) {
      fail(QStringLiteral("Opening the console blocked the interface for %1 ms").arg(openMs));
      return;
    }
    if (!checkConsoleView(QStringLiteral("first open"))) {
      return;
    }
    QMetaObject::invokeMethod(window, "leaveConsole");
    QTimer::singleShot(400, window, [=] {
      for (const VisibleCard& card : visibleCards()) {
        if (card.title.startsWith(QStringLiteral("SNES Cart"))) {
          fail(QStringLiteral("after leaving: cartridge '%1' is still visible").arg(card.title));
          return;
        }
      }
      const int secondPortal = portalRow();
      if (secondPortal < 0) {
        fail(QStringLiteral("after leaving: the Super Nintendo portal is gone"));
        return;
      }
      QMetaObject::invokeMethod(window, "openGame", Q_ARG(QVariant, QVariant(secondPortal)));
      QTimer::singleShot(400, window, [=] {
        if (!checkConsoleView(QStringLiteral("second open"))) return;
        QMetaObject::invokeMethod(window, "leaveConsole");
        if (application->arguments().contains(QStringLiteral("--expand-scroll-test")))
          library->setProperty("expandConsoles", true);
        QTimer::singleShot(150, window, [=] {
          // Removing the earlier non-emulated rows moves GridView's content origin.
          QMetaObject::invokeMethod(grid, "positionViewAtEnd");
          library->setProperty("sourceFilters", LibraryFilterModel::emulatorSources());
          grid->setProperty("currentIndex", 0);
          // Layout changes cancel a wheel scroll, and under parallel test load the filtered
          // grid can still be laying out after a fixed delay. Wait until it holds still.
          auto stablePolls = std::make_shared<int>(0);
          auto settleAttempts = std::make_shared<int>(0);
          auto lastShape = std::make_shared<QList<qreal>>();
          auto settle = std::make_shared<std::function<void()>>();
          *settle = [=] {
            QMetaObject::invokeMethod(grid, "positionViewAtBeginning");
            const QList<qreal> shape = {grid->property("count").toReal(),
                                        grid->property("originY").toReal(),
                                        grid->property("contentY").toReal(),
                                        grid->property("contentHeight").toReal()};
            *stablePolls = shape == *lastShape ? *stablePolls + 1 : 0;
            *lastShape = shape;
            if (*stablePolls < 2 && ++*settleAttempts < 60) {
              QTimer::singleShot(50, window, *settle);
              return;
            }
            const qreal origin = grid->property("originY").toReal();
            qInfo() << "Filtered grid origin:" << origin;
            if ((qFuzzyIsNull(origin) && !library->property("expandConsoles").toBool()) || visibleCards().isEmpty()) {
              fail(QStringLiteral("Wheel regression fixture did not create a shifted visible grid"));
              return;
            }
            const QPointF point = grid->mapToScene(QPointF(grid->width() / 2, grid->height() / 2));
            QWheelEvent wheel(point, window->mapToGlobal(point), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &wheel);
            // Poll for the scroll rather than guessing at a delay. The animated wheel can
            // take longer than a fixed wait when the suite runs tests in parallel.
            auto attempts = std::make_shared<int>(0);
            auto check = std::make_shared<std::function<void()>>();
            *check = [=] {
              const qreal y = grid->property("contentY").toReal();
              const qreal first = grid->property("originY").toReal();
              const qreal last = first + qMax(0.0, grid->property("contentHeight").toReal() - grid->height());
              if (visibleCards().isEmpty() || y < first - 1 || y > last + 1) {
                fail(QStringLiteral("Wheel after source filtering hid the cards: y=%1, bounds=%2..%3")
                         .arg(y).arg(first).arg(last));
                return;
              }
              // Wait for the wheel animation to land too, or it would overwrite the scrollbar
              // jump below.
              const qreal target = grid->property("wheelTargetY").toReal();
              if (last > first + 1 && (y <= first + 1 || qAbs(y - target) > 1)) {
                if (++*attempts < 60) {
                  QTimer::singleShot(50, window, *check);
                  return;
                }
                fail(QStringLiteral("Wheel did not scroll the expanded collection: y=%1, target=%2")
                         .arg(y).arg(target));
                return;
              }
              auto* track = window->findChild<QQuickItem*>(QStringLiteral("libraryScrollTrack"));
              if (last > first + 1 && track) {
                QMetaObject::invokeMethod(track, "scrollTo", Q_ARG(QVariant, track->height()));
                QTimer::singleShot(100, window, [=] {
                  if (visibleCards().isEmpty() || qAbs(grid->property("contentY").toReal() - last) > 1) {
                    fail(QStringLiteral("Scrollbar after filtering did not reach the visible last row"));
                    return;
                  }
                  application->quit();
                });
              } else {
                application->quit();
              }
            };
            QTimer::singleShot(250, window, *check);
          };
          QTimer::singleShot(150, window, *settle);
        });
      });
    });
  });
}

} // namespace

// --console-portal-test, and with --expand-scroll-test the expanded collection too. The
// cartridges behind the portal come from the console fixture in prepareBeforeSources.
void driveConsolePortal(Harness&, AppContext& app) {
  QQuickWindow* window = app.quickWindow;
  QGuiApplication* application = app.application;
  QTimer::singleShot(700, window, [window, application] {
    runConsolePortalTest(window, application);
  });
}

} // namespace SelfTest
