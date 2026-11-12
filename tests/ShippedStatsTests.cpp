#include "app/AppHarness.h"
#include "app/OmakadeApp.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdlib>

Q_IMPORT_QML_PLUGIN(OmakadePlugin)

// Opens Stats the way the shipped omakade binary does. omakade-uitest's fixtures set the
// context properties Stats reads before the QML loads, so they can hide a value the shipped
// app gets wrong. This harness leaves every stage alone and only drives the window: it asks
// for the card preview before Stats has loaded, opens Stats, and checks that the loader ran
// to the end without a QML error.
namespace {

QQuickItem* findItem(QQuickItem* item, const QString& name) {
  if (item == nullptr) return nullptr;
  if (item->objectName() == name) return item;
  for (QQuickItem* child : item->childItems())
    if (QQuickItem* found = findItem(child, name)) return found;
  return nullptr;
}

class ShippedHarness final : public AppHarness {
public:
  std::optional<int> start(AppContext&) override { return std::nullopt; }
  std::optional<int> prepare(Stage, AppContext&) override { return std::nullopt; }
  std::optional<int> windowReady(AppContext&) override { return std::nullopt; }

  bool drive(AppContext& app) override {
    QQuickWindow* window = app.quickWindow;
    if (window == nullptr) {
      qCritical() << "Shipped stats: no window";
      m_failed = true;
      QTimer::singleShot(0, app.application, &QCoreApplication::quit);
      return true;
    }
    QObject::connect(app.engine, &QQmlEngine::warnings, window,
                     [this](const QList<QQmlError>& warnings) {
                       // The runtime errors OMAKADE_TEST_QML_ERRORS lists, plus any exception
                       // a handler throws. A cover that fails to load is not one of them.
                       static const QRegularExpression error(QStringLiteral(
                           "Error:|TypeError|ReferenceError|RangeError|Binding loop|"
                           "Unable to assign|Cannot assign|is not a type|"
                           "Could not convert argument|Invalid write to global property"));
                       for (const QQmlError& warning : warnings) {
                         if (!warning.toString().contains(error)) continue;
                         qCritical().noquote() << "Shipped stats: QML error" << warning.toString();
                         m_failed = true;
                       }
                     });
    QTimer::singleShot(200, window, [this, window, &app] {
      window->setProperty("pendingCardPreview", true);
      window->setProperty("statsOpen", true);
      auto* poll = new QTimer(window);
      poll->setInterval(50);
      QObject::connect(poll, &QTimer::timeout, window, [this, window, poll, &app] {
        QQuickItem* loader = findItem(window->contentItem(), QStringLiteral("statsLoader"));
        QObject* stats = loader ? loader->property("item").value<QObject*>() : nullptr;
        const bool open = stats && stats->property("cardPreviewOpen").toBool();
        if (!open && ++m_attempts < 200) return;
        poll->stop();
        if (!open) {
          qCritical() << "Shipped stats: the pending card preview never opened."
                      << "loaded:" << (stats != nullptr);
          m_failed = true;
        } else if (stats->property("currentView").toInt() != 0) {
          qCritical() << "Shipped stats: Stats did not open on the overview";
          m_failed = true;
        }
        // Give a late error from the rest of onLoaded time to arrive before the run ends.
        QTimer::singleShot(300, app.application, &QCoreApplication::quit);
      });
      poll->start();
    });
    return true;
  }

  int finish(AppContext&, int result) override {
    if (m_failed) return EXIT_FAILURE;
    if (result == EXIT_SUCCESS) qInfo() << "Shipped stats: the card preview opened with no QML errors";
    return result;
  }

private:
  bool m_failed = false;
  int m_attempts = 0;
};

} // namespace

int main(int argc, char* argv[]) {
  // --smoke-test keeps the run on the demo library, with scratch settings, like the
  // omakade_qml_smoke test of the shipped binary.
  char smoke[] = "--smoke-test";
  char* arguments[] = {argc > 0 ? argv[0] : smoke, smoke, nullptr};
  ShippedHarness harness;
  return runOmakade(2, arguments, &harness);
}
