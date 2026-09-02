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
  static const QRegularExpression valid(QStringLiteral("^7656119[0-9]{10}$"));
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

bool AppSettings::pcsx2Enabled() const { return m_pcsx2Enabled; }

void AppSettings::setPcsx2Enabled(bool value) {
  const bool wasAuto = m_pcsx2Auto;
  m_pcsx2Auto = false;  // an explicit user choice disables automatic detection
  if (m_pcsx2Enabled == value && !wasAuto) {
    return;
  }
  m_pcsx2Enabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::ryujinxEnabled() const { return m_ryujinxEnabled; }

void AppSettings::setRyujinxEnabled(bool value) {
  const bool wasAuto = m_ryujinxAuto;
  m_ryujinxAuto = false;  // an explicit user choice disables automatic detection
  if (m_ryujinxEnabled == value && !wasAuto) {
    return;
  }
  m_ryujinxEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::pcsx2AutoEnabled() const { return m_pcsx2Auto; }

void AppSettings::setPcsx2AutoEnabled(bool value) { m_pcsx2Auto = value; }

bool AppSettings::ryujinxAutoEnabled() const { return m_ryujinxAuto; }

void AppSettings::setRyujinxAutoEnabled(bool value) { m_ryujinxAuto = value; }

bool AppSettings::closeAfterLaunch() const { return m_closeAfterLaunch; }

void AppSettings::setCloseAfterLaunch(bool value) {
  if (m_closeAfterLaunch == value) {
    return;
  }
  m_closeAfterLaunch = value;
  save();
  emit closeAfterLaunchChanged();
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
  const QRegularExpression steamId(
      QStringLiteral("(?m)^steam_id\\s*=\\s*\"(7656119[0-9]{10})\"\\s*$"));
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
  const QRegularExpression pcsx2Key(
      QStringLiteral("(?m)^pcsx2_enabled\\s*=\\s*(true|false)\\s*$"));
  m_pcsx2Auto = !pcsx2Key.match(contents).hasMatch();
  m_pcsx2Enabled = readEnabled(QStringLiteral("pcsx2_enabled"), false);
  const QRegularExpression ryujinxKey(
      QStringLiteral("(?m)^ryujinx_enabled\\s*=\\s*(true|false)\\s*$"));
  m_ryujinxAuto = !ryujinxKey.match(contents).hasMatch();
  m_ryujinxEnabled = readEnabled(QStringLiteral("ryujinx_enabled"), false);
  m_closeAfterLaunch = readEnabled(QStringLiteral("close_after_launch"), false);
  m_sunshineOmakadeApp = readEnabled(QStringLiteral("sunshine_omakade_app"), false);
  m_sunshineGameApps = readEnabled(QStringLiteral("sunshine_game_apps"), false);
}

bool AppSettings::sunshineOmakadeApp() const { return m_sunshineOmakadeApp; }

void AppSettings::setSunshineOmakadeApp(bool value) {
  if (m_sunshineOmakadeApp == value) {
    return;
  }
  m_sunshineOmakadeApp = value;
  save();
  emit sunshineChanged();
}

bool AppSettings::sunshineGameApps() const { return m_sunshineGameApps; }

void AppSettings::setSunshineGameApps(bool value) {
  if (m_sunshineGameApps == value) {
    return;
  }
  m_sunshineGameApps = value;
  save();
  emit sunshineChanged();
}

void AppSettings::save() const {
  QDir().mkpath(QFileInfo(m_path).absolutePath());
  QSaveFile file(m_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return;
  }
  // Emulator source keys are written only once their state is explicit (detection
  // completed or the user chose a value); while auto-detection is pending the keys
  // stay absent so a later emulator installation can still enable its source.
  QString contents =
      QStringLiteral("reduced_motion = %1\nartwork_cache_limit_mb = %2\nsteam_id = \"%3\"\n"
                     "igdb_client_id = \"%4\"\nsteam_enabled = %5\nlutris_enabled = %6\n"
                     "heroic_enabled = %7\nfaugus_enabled = %8\nretroarch_enabled = %9\n")
          .arg(m_reducedMotion ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_artworkCacheLimitMb)
          .arg(m_steamId)
          .arg(m_igdbClientId)
          .arg(m_steamEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_lutrisEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_heroicEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_faugusEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_retroArchEnabled ? QStringLiteral("true") : QStringLiteral("false"));
  if (!m_pcsx2Auto) {
    contents += QStringLiteral("pcsx2_enabled = %1\n")
                    .arg(m_pcsx2Enabled ? QStringLiteral("true") : QStringLiteral("false"));
  }
  if (!m_ryujinxAuto) {
    contents += QStringLiteral("ryujinx_enabled = %1\n")
                    .arg(m_ryujinxEnabled ? QStringLiteral("true") : QStringLiteral("false"));
  }
  contents += QStringLiteral("close_after_launch = %1\n"
                             "sunshine_omakade_app = %2\nsunshine_game_apps = %3\n")
                  .arg(m_closeAfterLaunch ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(m_sunshineOmakadeApp ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(m_sunshineGameApps ? QStringLiteral("true") : QStringLiteral("false"));
  file.write(contents.toUtf8());
  file.commit();
}
