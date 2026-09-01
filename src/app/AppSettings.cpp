#include "app/AppSettings.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

AppSettings::AppSettings(const QString& path, QObject* parent)
    : QObject(parent), m_path(path.isEmpty() ? defaultPath() : path) {
  load();
}

bool AppSettings::reducedMotion() const { return m_reducedMotion; }

void AppSettings::setReducedMotion(bool value) {
  if (m_reducedMotion == value) {
    return;
  }
  m_reducedMotion = value;
  save();
  emit reducedMotionChanged();
}

int AppSettings::artworkCacheLimitMb() const { return m_artworkCacheLimitMb; }

void AppSettings::setArtworkCacheLimitMb(int value) {
  value = qBound(128, value, 8192);
  if (m_artworkCacheLimitMb == value) {
    return;
  }
  m_artworkCacheLimitMb = value;
  save();
  emit artworkCacheLimitMbChanged();
}

QString AppSettings::steamId() const { return m_steamId; }

void AppSettings::setSteamId(const QString& value) {
  static const QRegularExpression valid(QStringLiteral("^[0-9]{5,20}$"));
  const QString normalized = value.trimmed();
  if ((!normalized.isEmpty() && !valid.match(normalized).hasMatch()) || m_steamId == normalized) {
    return;
  }
  m_steamId = normalized;
  save();
  emit steamIdChanged();
}

QString AppSettings::igdbClientId() const { return m_igdbClientId; }

void AppSettings::setIgdbClientId(const QString& value) {
  static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9]{5,64}$"));
  const QString normalized = value.trimmed();
  if ((!normalized.isEmpty() && !valid.match(normalized).hasMatch()) ||
      m_igdbClientId == normalized) {
    return;
  }
  m_igdbClientId = normalized;
  save();
  emit igdbClientIdChanged();
}

bool AppSettings::steamEnabled() const { return m_steamEnabled; }

void AppSettings::setSteamEnabled(bool value) {
  if (m_steamEnabled == value) {
    return;
  }
  m_steamEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::lutrisEnabled() const { return m_lutrisEnabled; }

void AppSettings::setLutrisEnabled(bool value) {
  if (m_lutrisEnabled == value) {
    return;
  }
  m_lutrisEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::heroicEnabled() const { return m_heroicEnabled; }

void AppSettings::setHeroicEnabled(bool value) {
  if (m_heroicEnabled == value) {
    return;
  }
  m_heroicEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::faugusEnabled() const { return m_faugusEnabled; }

void AppSettings::setFaugusEnabled(bool value) {
  if (m_faugusEnabled == value) {
    return;
  }
  m_faugusEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::retroArchEnabled() const { return m_retroArchEnabled; }

void AppSettings::setRetroArchEnabled(bool value) {
  if (m_retroArchEnabled == value) {
    return;
  }
  m_retroArchEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::closeAfterLaunch() const { return m_closeAfterLaunch; }

void AppSettings::setCloseAfterLaunch(bool value) {
  if (m_closeAfterLaunch == value) {
    return;
  }
  m_closeAfterLaunch = value;
  save();
  emit closeAfterLaunchChanged();
}

bool AppSettings::steamOwnedGames() const { return m_steamOwnedGames; }

void AppSettings::setSteamOwnedGames(bool value) {
  if (m_steamOwnedGames == value) {
    return;
  }
  m_steamOwnedGames = value;
  save();
  emit steamOwnedGamesChanged();
}

QString AppSettings::defaultPath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
         QStringLiteral("/omakade/config.toml");
}

void AppSettings::load() {
  QFile file(m_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  const QString contents = QString::fromUtf8(file.readAll());
  const QRegularExpression motion(QStringLiteral("(?m)^reduced_motion\\s*=\\s*(true|false)\\s*$"));
  const QRegularExpressionMatch motionMatch = motion.match(contents);
  if (motionMatch.hasMatch()) {
    m_reducedMotion = motionMatch.captured(1) == QStringLiteral("true");
  }
  const QRegularExpression limit(QStringLiteral("(?m)^artwork_cache_limit_mb\\s*=\\s*(\\d+)\\s*$"));
  const QRegularExpressionMatch limitMatch = limit.match(contents);
  if (limitMatch.hasMatch()) {
    m_artworkCacheLimitMb = qBound(128, limitMatch.captured(1).toInt(), 8192);
  }
  const QRegularExpression steamId(QStringLiteral("(?m)^steam_id\\s*=\\s*\"([0-9]{5,20})\"\\s*$"));
  const QRegularExpressionMatch steamIdMatch = steamId.match(contents);
  if (steamIdMatch.hasMatch()) {
    m_steamId = steamIdMatch.captured(1);
  }
  const QRegularExpression igdbClientId(
      QStringLiteral("(?m)^igdb_client_id\\s*=\\s*\"([A-Za-z0-9]{5,64})\"\\s*$"));
  const QRegularExpressionMatch igdbClientIdMatch = igdbClientId.match(contents);
  if (igdbClientIdMatch.hasMatch()) {
    m_igdbClientId = igdbClientIdMatch.captured(1);
  }
  const auto readEnabled = [&contents](const QString& key, bool fallback) {
    const QRegularExpression expression(
        QStringLiteral("(?m)^%1\\s*=\\s*(true|false)\\s*$").arg(key));
    const QRegularExpressionMatch match = expression.match(contents);
    return match.hasMatch() ? match.captured(1) == QStringLiteral("true") : fallback;
  };
  m_steamEnabled = readEnabled(QStringLiteral("steam_enabled"), true);
  m_lutrisEnabled = readEnabled(QStringLiteral("lutris_enabled"), true);
  m_heroicEnabled = readEnabled(QStringLiteral("heroic_enabled"), true);
  m_faugusEnabled = readEnabled(QStringLiteral("faugus_enabled"), true);
  m_retroArchEnabled = readEnabled(QStringLiteral("retroarch_enabled"), true);
  m_closeAfterLaunch = readEnabled(QStringLiteral("close_after_launch"), false);
  m_steamOwnedGames = readEnabled(QStringLiteral("steam_owned_games"), true);
}

void AppSettings::save() const {
  QDir().mkpath(QFileInfo(m_path).absolutePath());
  QSaveFile file(m_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return;
  }
  file.write(QStringLiteral("reduced_motion = %1\nartwork_cache_limit_mb = %2\nsteam_id = "
                            "\"%3\"\nigdb_client_id = \"%4\"\nsteam_enabled = %5\n"
                            "lutris_enabled = %6\nheroic_enabled = %7\nfaugus_enabled = %8\n"
                            "retroarch_enabled = %9\nclose_after_launch = %10\n"
                            "steam_owned_games = %11\n")
                 .arg(m_reducedMotion ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(m_artworkCacheLimitMb)
                 .arg(m_steamId)
                 .arg(m_igdbClientId)
                 .arg(m_steamEnabled ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(m_lutrisEnabled ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(m_heroicEnabled ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(m_faugusEnabled ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(m_retroArchEnabled ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(m_closeAfterLaunch ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(m_steamOwnedGames ? QStringLiteral("true") : QStringLiteral("false"))
                 .toUtf8());
  file.commit();
}
