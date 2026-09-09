#include "launch/PlayRequest.h"

#include "launch/GameLauncher.h"
#include "library/UnifiedGameModel.h"

#include <QEventLoop>
#include <QTimer>

LaunchKey LaunchKey::parse(const QString& text) {
  const QString trimmed = text.trimmed();
  const qsizetype first = trimmed.indexOf(QLatin1Char(':'));
  if (first < 0) {
    return {};
  }
  const qsizetype second = trimmed.indexOf(QLatin1Char(':'), first + 1);
  if (second < 0) {
    return {};
  }
  return {.source = trimmed.left(first),
          .runner = trimmed.mid(first + 1, second - first - 1),
          .appId = trimmed.mid(second + 1)};
}

QString LaunchKey::toString() const {
  return source + QLatin1Char(':') + runner + QLatin1Char(':') + appId;
}

namespace {
QString normalizedRunner(const QVariant& value) {
  const QString runner = value.toString();
  return runner.isNull() ? QStringLiteral("") : runner;
}
} // namespace

QVariantMap PlayRequest::findInstallation(const UnifiedGameModel& games, const LaunchKey& key,
                                          int* row) {
  if (row != nullptr) {
    *row = -1;
  }
  if (!key.isValid()) {
    return {};
  }
  for (int candidate = 0; candidate < games.rowCount(); ++candidate) {
    for (const QVariant& value : games.installations(candidate)) {
      const QVariantMap installation = value.toMap();
      if (installation.value(QStringLiteral("source")).toString().compare(
              key.source, Qt::CaseInsensitive) != 0 ||
          installation.value(QStringLiteral("appId")).toString() != key.appId ||
          normalizedRunner(installation.value(QStringLiteral("runner"))) != key.runner) {
        continue;
      }
      if (row != nullptr) {
        *row = candidate;
      }
      return installation;
    }
  }
  return {};
}

bool PlayRequest::waitForInstallation(UnifiedGameModel& games, const LaunchKey& key,
                                      int timeoutMs) {
  if (!key.isValid()) {
    return false;
  }
  if (!findInstallation(games, key, nullptr).isEmpty()) {
    return true;
  }

  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  const auto retry = [&] {
    if (!findInstallation(games, key, nullptr).isEmpty()) {
      loop.quit();
    }
  };
  QObject::connect(&games, &QAbstractItemModel::modelReset, &loop, retry);
  QObject::connect(&games, &QAbstractItemModel::rowsInserted, &loop, retry);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  timeout.start(qMax(0, timeoutMs));
  loop.exec();
  return !findInstallation(games, key, nullptr).isEmpty();
}

bool PlayRequest::perform(UnifiedGameModel& games, GameLauncher& launcher, const LaunchKey& key,
                          QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!key.isValid()) {
    return fail(QStringLiteral("Expected a game key like Steam::620"));
  }
  int row = -1;
  const QVariantMap installation = findInstallation(games, key, &row);
  if (installation.isEmpty()) {
    return fail(QStringLiteral("%1 is not in the Omakade library").arg(key.toString()));
  }
  const QString title = installation.value(QStringLiteral("title")).toString();
  if (installation.contains(QStringLiteral("installed")) &&
      !installation.value(QStringLiteral("installed")).toBool()) {
    return fail(QStringLiteral("%1 is not installed").arg(title));
  }
  const QString source = installation.value(QStringLiteral("source")).toString();
  const QString runner = normalizedRunner(installation.value(QStringLiteral("runner")));
  const QString appId = installation.value(QStringLiteral("appId")).toString();
  if (!launcher.launch(source, appId, installation.value(QStringLiteral("flatpak")).toBool(),
                       runner, installation.value(QStringLiteral("installPath")).toString(),
                       installation.value(QStringLiteral("launchTarget")).toString(),
                       installation.value(QStringLiteral("system")).toString())) {
    return fail(launcher.lastError());
  }
  games.recordLaunch(row, source, runner, appId);
  return true;
}
