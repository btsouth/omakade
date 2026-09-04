#include "theme/OmarchyTheme.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <cmath>
namespace {
QString defaultStateHome() {
  const QString xdgStateHome = qEnvironmentVariable("XDG_STATE_HOME");
  return xdgStateHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/state")
                                : xdgStateHome;
}

QString defaultConfigHome() {
  const QString xdgConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME");
  return xdgConfigHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.config")
                                 : xdgConfigHome;
}

double linearChannel(double channel) {
  channel /= 255.0;
  return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}
} // namespace

OmarchyTheme::OmarchyTheme(QObject* parent)
    : OmarchyTheme(defaultStateHome(), defaultConfigHome(), parent) {}

OmarchyTheme::OmarchyTheme(QString stateHome, QString configHome, QObject* parent)
    : QObject(parent), m_stateHome(std::move(stateHome)), m_configHome(std::move(configHome)) {
  m_reloadTimer.setSingleShot(true);
  m_reloadTimer.setInterval(60);

  connect(&m_reloadTimer, &QTimer::timeout, this, &OmarchyTheme::reload);
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleReload(); });
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleReload(); });

  reload();
}

OmarchyTheme::~OmarchyTheme() {
  for (QProcess* process : findChildren<QProcess*>()) {
    if (process->state() == QProcess::NotRunning) {
      continue;
    }
    process->disconnect(this);
    process->terminate();
    if (!process->waitForFinished(50)) {
      process->kill();
      process->waitForFinished(50);
    }
  }
}

bool OmarchyTheme::omarchyAvailable() const { return m_omarchyAvailable; }
QString OmarchyTheme::themeName() const { return m_themeName; }
QString OmarchyTheme::mode() const { return m_mode; }
QString OmarchyTheme::fontFamily() const { return m_fontFamily; }
int OmarchyTheme::cornerRadius() const { return m_cornerRadius; }
int OmarchyTheme::gapsOut() const { return m_gapsOut; }
qreal OmarchyTheme::surfaceAlpha() const { return m_surfaceAlpha; }
QColor OmarchyTheme::accent() const { return m_accent; }
QColor OmarchyTheme::selection() const { return m_selection; }
QColor OmarchyTheme::muted() const { return m_muted; }
QColor OmarchyTheme::background() const { return m_background; }
QColor OmarchyTheme::darkBackground() const { return m_darkBackground; }
QColor OmarchyTheme::darkerBackground() const { return m_darkerBackground; }
QColor OmarchyTheme::lighterBackground() const { return m_lighterBackground; }
QColor OmarchyTheme::foreground() const { return m_foreground; }
QColor OmarchyTheme::darkForeground() const { return m_darkForeground; }
QColor OmarchyTheme::lightForeground() const { return m_lightForeground; }
QColor OmarchyTheme::brightForeground() const { return m_brightForeground; }
QColor OmarchyTheme::mutedText() const { return m_mutedText; }
QColor OmarchyTheme::red() const { return m_red; }
QColor OmarchyTheme::yellow() const { return m_yellow; }
QColor OmarchyTheme::green() const { return m_green; }
QColor OmarchyTheme::cyan() const { return m_cyan; }
QColor OmarchyTheme::blue() const { return m_blue; }
QColor OmarchyTheme::magenta() const { return m_magenta; }

void OmarchyTheme::reload() {
  applyFallback();

  const QString colorsPath = themeRoot() + QStringLiteral("/colors.toml");
  Values values = readSimpleToml(colorsPath);
  m_omarchyAvailable = !values.isEmpty();
  if (m_omarchyAvailable) {
    if (!values.contains(QStringLiteral("mode")) &&
        !values.contains(QStringLiteral("theme_type")) &&
        QFileInfo::exists(themeRoot() + QStringLiteral("/light.mode"))) {
      values.insert(QStringLiteral("mode"), QStringLiteral("light"));
    }
    values = resolvedValues(std::move(values));
    applyValues(values);
    m_surfaceAlpha =
        readSectionAlpha(themeRoot() + QStringLiteral("/shell.toml"), QStringLiteral("launcher"),
                         QStringLiteral("background-alpha"), m_surfaceAlpha);

    QFile nameFile(currentRoot() + QStringLiteral("/theme.name"));
    if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      const QString slug = QString::fromUtf8(nameFile.readAll()).trimmed();
      if (!slug.isEmpty()) {
        m_themeName = slug;
        m_themeName.replace(QLatin1Char('-'), QLatin1Char(' '));
        const QStringList words = m_themeName.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QStringList titled;
        titled.reserve(words.size());
        for (const QString& word : words) {
          QString transformed = word;
          transformed[0] = transformed.at(0).toUpper();
          titled.push_back(transformed);
        }
        m_themeName = titled.join(QLatin1Char(' '));
      }
    }
  }

  m_surfaceAlpha = readSectionAlpha(m_configHome + QStringLiteral("/omarchy/shell.toml"),
                                    QStringLiteral("launcher"), QStringLiteral("background-alpha"),
                                    m_surfaceAlpha);

  m_fontFamily = resolvedMonospaceFamily();
  refreshHyprlandMetrics();
  refreshWatchPaths();
  emit themeChanged();
}

QString OmarchyTheme::currentRoot() const {
  QString stateRoot = m_stateHome + QStringLiteral("/omarchy/current");
  if (QFileInfo::exists(stateRoot + QStringLiteral("/theme/colors.toml"))) {
    return stateRoot;
  }

  QString configRoot = m_configHome + QStringLiteral("/omarchy/current");
  if (QFileInfo::exists(configRoot + QStringLiteral("/theme/colors.toml"))) {
    return configRoot;
  }

  return stateRoot;
}

QString OmarchyTheme::themeRoot() const { return currentRoot() + QStringLiteral("/theme"); }

OmarchyTheme::Values OmarchyTheme::readSimpleToml(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }

  static const QRegularExpression assignment(
      QStringLiteral(R"(^\s*([A-Za-z0-9_]+)\s*=\s*["']([^"']+)["']\s*(?:#.*)?$)"));
  Values values;
  QTextStream stream(&file);
  while (!stream.atEnd()) {
    const QRegularExpressionMatch match = assignment.match(stream.readLine());
    if (match.hasMatch()) {
      values.insert(match.captured(1), match.captured(2));
    }
  }
  return values;
}

OmarchyTheme::Values OmarchyTheme::resolvedValues(Values values) {
  const auto alias = [&values](const QString& key, const QString& fallback) {
    if (!values.contains(key) && values.contains(fallback)) {
      values.insert(key, values.value(fallback));
    }
  };

  const QList<QPair<QString, QString>> legacyNames = {
      {QStringLiteral("background"), QStringLiteral("bg")},
      {QStringLiteral("dark_background"), QStringLiteral("dark_bg")},
      {QStringLiteral("darker_background"), QStringLiteral("darker_bg")},
      {QStringLiteral("lighter_background"), QStringLiteral("lighter_bg")},
      {QStringLiteral("foreground"), QStringLiteral("fg")},
      {QStringLiteral("dark_foreground"), QStringLiteral("dark_fg")},
      {QStringLiteral("light_foreground"), QStringLiteral("light_fg")},
      {QStringLiteral("bright_foreground"), QStringLiteral("bright_fg")},
  };
  for (const auto& [key, fallback] : legacyNames) {
    alias(key, fallback);
  }

  alias(QStringLiteral("background"), QStringLiteral("color0"));
  alias(QStringLiteral("foreground"), QStringLiteral("color7"));
  if (values.contains(QStringLiteral("background"))) {
    values.insert(QStringLiteral("color0"), values.value(QStringLiteral("background")));
  }
  if (values.contains(QStringLiteral("foreground"))) {
    values.insert(QStringLiteral("color7"), values.value(QStringLiteral("foreground")));
  }

  const QList<QPair<QString, QString>> ansiNames = {
      {QStringLiteral("red"), QStringLiteral("color1")},
      {QStringLiteral("green"), QStringLiteral("color2")},
      {QStringLiteral("yellow"), QStringLiteral("color3")},
      {QStringLiteral("blue"), QStringLiteral("color4")},
      {QStringLiteral("magenta"), QStringLiteral("color5")},
      {QStringLiteral("cyan"), QStringLiteral("color6")},
  };
  for (const auto& [key, fallback] : ansiNames) {
    alias(key, fallback);
  }
  alias(QStringLiteral("magenta"), QStringLiteral("purple"));
  alias(QStringLiteral("accent"), QStringLiteral("color4"));

  alias(QStringLiteral("light_foreground"), QStringLiteral("color7"));
  alias(QStringLiteral("light_foreground"), QStringLiteral("foreground"));
  alias(QStringLiteral("bright_foreground"), QStringLiteral("color15"));
  alias(QStringLiteral("bright_foreground"), QStringLiteral("foreground"));
  alias(QStringLiteral("lighter_background"), QStringLiteral("color0"));
  alias(QStringLiteral("lighter_background"), QStringLiteral("background"));
  alias(QStringLiteral("dark_foreground"), QStringLiteral("color8"));
  alias(QStringLiteral("dark_foreground"), QStringLiteral("foreground"));
  alias(QStringLiteral("muted"), QStringLiteral("color8"));
  alias(QStringLiteral("muted"), QStringLiteral("dark_foreground"));
  alias(QStringLiteral("selection"), QStringLiteral("selection_background"));
  alias(QStringLiteral("selection"), QStringLiteral("color8"));
  alias(QStringLiteral("selection"), QStringLiteral("color0"));
  alias(QStringLiteral("selection"), QStringLiteral("background"));

  const auto deriveShade = [&values](const QString& key, qreal amount) {
    if (values.contains(key)) {
      return;
    }
    const QColor background(values.value(QStringLiteral("background")));
    if (!background.isValid()) {
      return;
    }
    const auto channel = [amount](int value) { return qRound(value * (1.0 - amount)); };
    values.insert(key, QColor(channel(background.red()), channel(background.green()),
                              channel(background.blue()))
                           .name());
  };
  deriveShade(QStringLiteral("dark_background"), 0.25);
  deriveShade(QStringLiteral("darker_background"), 0.5);

  alias(QStringLiteral("mode"), QStringLiteral("theme_type"));
  if (!values.contains(QStringLiteral("mode"))) {
    const QColor background(values.value(QStringLiteral("background")));
    const bool light =
        background.isValid() && background.red() + background.green() + background.blue() > 382;
    values.insert(QStringLiteral("mode"), light ? QStringLiteral("light") : QStringLiteral("dark"));
  }

  return values;
}

qreal OmarchyTheme::readSectionAlpha(const QString& path, const QString& section,
                                     const QString& key, qreal fallback) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fallback;
  }
  const QRegularExpression sectionPattern(QStringLiteral(R"(^\s*\[([^]]+)\]\s*(?:#.*)?$)"));
  const QRegularExpression assignment(
      QStringLiteral(R"(^\s*([A-Za-z0-9_-]+)\s*=\s*([0-9]*\.?[0-9]+)\s*(?:#.*)?$)"));
  QString currentSection;
  QTextStream stream(&file);
  while (!stream.atEnd()) {
    const QString line = stream.readLine();
    const QRegularExpressionMatch sectionMatch = sectionPattern.match(line);
    if (sectionMatch.hasMatch()) {
      currentSection = sectionMatch.captured(1);
      continue;
    }
    if (currentSection != section) {
      continue;
    }
    const QRegularExpressionMatch valueMatch = assignment.match(line);
    if (valueMatch.hasMatch() && valueMatch.captured(1) == key) {
      bool okay = false;
      const qreal value = valueMatch.captured(2).toDouble(&okay);
      return okay ? std::clamp(value, 0.0, 1.0) : fallback;
    }
  }
  return fallback;
}

QColor OmarchyTheme::parsedColor(const Values& values, const QString& key, const QColor& fallback) {
  const QColor candidate(values.value(key));
  return candidate.isValid() ? candidate : fallback;
}

QString OmarchyTheme::resolvedMonospaceFamily() {
  const QFontInfo resolved(QFont(QStringLiteral("monospace")));
  return resolved.family().isEmpty() ? QStringLiteral("monospace") : resolved.family();
}

double OmarchyTheme::contrastRatio(const QColor& first, const QColor& second) {
  const auto luminance = [](const QColor& color) {
    return 0.2126 * linearChannel(color.red()) + 0.7152 * linearChannel(color.green()) +
           0.0722 * linearChannel(color.blue());
  };
  const double firstLuminance = luminance(first);
  const double secondLuminance = luminance(second);
  const double brightest = std::max(firstLuminance, secondLuminance);
  const double darkest = std::min(firstLuminance, secondLuminance);
  return (brightest + 0.05) / (darkest + 0.05);
}

void OmarchyTheme::applyFallback() {
  m_omarchyAvailable = false;
  m_themeName = QStringLiteral("Omakade Dark");
  m_mode = QStringLiteral("dark");
  m_surfaceAlpha = 0.82;
  m_accent = QColor(QStringLiteral("#78a98f"));
  m_selection = QColor(QStringLiteral("#31453b"));
  m_muted = QColor(QStringLiteral("#53685b"));
  m_background = QColor(QStringLiteral("#111c18"));
  m_darkBackground = QColor(QStringLiteral("#0c1512"));
  m_darkerBackground = QColor(QStringLiteral("#090f0d"));
  m_lighterBackground = QColor(QStringLiteral("#23372b"));
  m_foreground = QColor(QStringLiteral("#c1c497"));
  m_darkForeground = QColor(QStringLiteral("#81b8a8"));
  m_lightForeground = QColor(QStringLiteral("#d6d5bc"));
  m_brightForeground = QColor(QStringLiteral("#f7e8b2"));
  m_mutedText = m_darkForeground;
  m_red = QColor(QStringLiteral("#ff7166"));
  m_yellow = QColor(QStringLiteral("#e5c736"));
  m_green = QColor(QStringLiteral("#63b07a"));
  m_cyan = QColor(QStringLiteral("#8cd3cb"));
  m_blue = QColor(QStringLiteral("#7da6ff"));
  m_magenta = QColor(QStringLiteral("#d2689c"));
}

void OmarchyTheme::applyValues(const Values& values) {
  m_mode = values.value(QStringLiteral("mode"), m_mode);
  m_accent = parsedColor(values, QStringLiteral("accent"), m_accent);
  m_selection = parsedColor(values, QStringLiteral("selection"), m_selection);
  m_muted = parsedColor(values, QStringLiteral("muted"), m_muted);
  m_background = parsedColor(values, QStringLiteral("background"), m_background);
  m_darkBackground = parsedColor(values, QStringLiteral("dark_background"), m_darkBackground);
  m_darkerBackground = parsedColor(values, QStringLiteral("darker_background"), m_darkerBackground);
  m_lighterBackground =
      parsedColor(values, QStringLiteral("lighter_background"), m_lighterBackground);
  m_foreground = parsedColor(values, QStringLiteral("foreground"), m_foreground);
  m_darkForeground = parsedColor(values, QStringLiteral("dark_foreground"), m_darkForeground);
  m_lightForeground = parsedColor(values, QStringLiteral("light_foreground"), m_lightForeground);
  m_brightForeground = parsedColor(values, QStringLiteral("bright_foreground"), m_brightForeground);
  m_red = parsedColor(values, QStringLiteral("red"), m_red);
  m_yellow = parsedColor(values, QStringLiteral("yellow"), m_yellow);
  m_green = parsedColor(values, QStringLiteral("green"), m_green);
  m_cyan = parsedColor(values, QStringLiteral("cyan"), m_cyan);
  m_blue = parsedColor(values, QStringLiteral("blue"), m_blue);
  m_magenta = parsedColor(values, QStringLiteral("magenta"), m_magenta);

  m_mutedText =
      contrastRatio(m_darkForeground, m_background) >= 3.0 ? m_darkForeground : m_lightForeground;
}

void OmarchyTheme::refreshHyprlandMetrics() {
  // hyprctl answers in a few milliseconds, but a stalled compositor would block the whole
  // interface for the timeout, so ask asynchronously and apply the answers when they land.
  const QString executable = QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
  if (executable.isEmpty() || qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()) {
    return;
  }
  const auto query = [this, executable](const QString& option, const auto& apply) {
    auto* process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, apply](int exitCode, QProcess::ExitStatus status) {
              const QJsonDocument document =
                  QJsonDocument::fromJson(process->readAllStandardOutput());
              process->deleteLater();
              if (status == QProcess::NormalExit && exitCode == 0 && document.isObject() &&
                  apply(document.object())) {
                emit themeChanged();
              }
            });
    connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    process->start(executable, {QStringLiteral("getoption"), option, QStringLiteral("-j")});
  };
  query(QStringLiteral("decoration:rounding"), [this](const QJsonObject& rounding) {
    if (!rounding.contains(QStringLiteral("int"))) {
      return false;
    }
    const int radius = std::max(0, rounding.value(QStringLiteral("int")).toInt());
    const bool changed = radius != m_cornerRadius;
    m_cornerRadius = radius;
    return changed;
  });
  query(QStringLiteral("general:gaps_out"), [this](const QJsonObject& gaps) {
    static const QRegularExpression number(QStringLiteral(R"((-?\d+(?:\.\d+)?))"));
    const QRegularExpressionMatch match =
        number.match(gaps.value(QStringLiteral("css")).toString());
    if (!match.hasMatch()) {
      return false;
    }
    const int gap = std::max(0, qRound(match.captured(1).toDouble() / 2.0));
    const bool changed = gap != m_gapsOut;
    m_gapsOut = gap;
    return changed;
  });
}

void OmarchyTheme::refreshWatchPaths() {
  if (!m_watcher.files().isEmpty()) {
    m_watcher.removePaths(m_watcher.files());
  }
  if (!m_watcher.directories().isEmpty()) {
    m_watcher.removePaths(m_watcher.directories());
  }

  // Watch the home roots only until their omarchy directories exist; otherwise every file
  // any application drops into ~/.config or ~/.local/state would trigger a theme reload.
  const QString stateOmarchy = m_stateHome + QStringLiteral("/omarchy");
  const QString configOmarchy = m_configHome + QStringLiteral("/omarchy");
  const QStringList candidates = {
      QFileInfo::exists(stateOmarchy) ? stateOmarchy : m_stateHome,
      QFileInfo::exists(configOmarchy) ? configOmarchy : m_configHome,
      currentRoot(),
      themeRoot(),
      themeRoot() + QStringLiteral("/colors.toml"),
      themeRoot() + QStringLiteral("/light.mode"),
      themeRoot() + QStringLiteral("/shell.toml"),
      currentRoot() + QStringLiteral("/theme.name"),
      m_configHome + QStringLiteral("/omarchy/shell.toml"),
      m_configHome + QStringLiteral("/fontconfig"),
      m_configHome + QStringLiteral("/fontconfig/fonts.conf"),
  };

  QStringList existing;
  for (const QString& path : candidates) {
    if (QFileInfo::exists(path)) {
      existing.push_back(path);
    }
  }
  if (!existing.isEmpty()) {
    m_watcher.addPaths(existing);
  }
}

void OmarchyTheme::scheduleReload() { m_reloadTimer.start(); }
