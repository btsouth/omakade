#include "launch/SteamLauncher.h"

#include <QDesktopServices>
#include <QRegularExpression>

namespace {
bool validAppId(const QString& appId) {
  static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]*$"));
  return digits.match(appId).hasMatch();
}
} // namespace

SteamLauncher::SteamLauncher(QObject* parent) : QObject(parent) {}

QString SteamLauncher::lastError() const { return m_lastError; }

QUrl SteamLauncher::launchUrl(const QString& appId) {
  return validAppId(appId) ? QUrl(QStringLiteral("steam://rungameid/%1").arg(appId)) : QUrl{};
}

QUrl SteamLauncher::manageUrl(const QString& appId) {
  return validAppId(appId) ? QUrl(QStringLiteral("steam://nav/games/details/%1").arg(appId))
                           : QUrl{};
}

QUrl SteamLauncher::installUrl(const QString& appId) {
  return validAppId(appId) ? QUrl(QStringLiteral("steam://install/%1").arg(appId)) : QUrl{};
}

bool SteamLauncher::launch(const QString& appId) { return open(launchUrl(appId)); }

bool SteamLauncher::manage(const QString& appId) { return open(manageUrl(appId)); }

bool SteamLauncher::install(const QString& appId) { return open(installUrl(appId)); }

bool SteamLauncher::open(const QUrl& url) {
  QString error;
  if (!url.isValid() || url.isEmpty()) {
    error = QStringLiteral("This game has an invalid Steam App ID.");
  } else if (!QDesktopServices::openUrl(url)) {
    error = QStringLiteral("Steam could not open the game. Check that Steam is installed.");
  }
  if (m_lastError != error) {
    m_lastError = error;
    emit lastErrorChanged();
  }
  return error.isEmpty();
}
