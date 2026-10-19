#include "sources/ports/PortCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {
QStringList strings(const QJsonValue& value) {
  QStringList list;
  for (const QJsonValue& item : value.toArray()) {
    const QString text = item.toString().trimmed();
    if (!text.isEmpty()) {
      list.append(text);
    }
  }
  return list;
}

bool isWordCharacter(QChar character) { return character.isLetterOrNumber(); }
} // namespace

const PortCatalog& PortCatalog::bundled() {
  static const PortCatalog catalog = [] {
    QFile file(QStringLiteral(":/ports/native-ports.json"));
    if (!file.open(QIODevice::ReadOnly)) {
      qWarning("The native port catalog is missing from this build");
      return PortCatalog{};
    }
    QString error;
    PortCatalog loaded = fromJson(file.readAll(), &error);
    if (!error.isEmpty()) {
      qWarning().noquote() << "Native port catalog:" << error;
    }
    return loaded;
  }();
  return catalog;
}

PortCatalog PortCatalog::fromJson(const QByteArray& json, QString* error) {
  PortCatalog catalog;
  QStringList problems;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    problems.append(QStringLiteral("not a JSON object"));
  } else if (document.object().value(QStringLiteral("schema")).toInt() != 1) {
    problems.append(QStringLiteral("unknown schema"));
  } else {
    static const QRegularExpression validId(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
    QSet<QString> ids;
    for (const QJsonValue& value : document.object().value(QStringLiteral("ports")).toArray()) {
      const QJsonObject object = value.toObject();
      NativePort port{.id = object.value(QStringLiteral("id")).toString(),
                      .name = object.value(QStringLiteral("name")).toString().trimmed(),
                      .game = object.value(QStringLiteral("game")).toString().trimmed(),
                      .system = object.value(QStringLiteral("system")).toString().trimmed(),
                      .repo = object.value(QStringLiteral("repo")).toString().trimmed(),
                      .linux = object.value(QStringLiteral("linux")).toBool(),
                      .serials = strings(object.value(QStringLiteral("serials"))),
                      .titles = strings(object.value(QStringLiteral("titles"))),
                      .files = strings(object.value(QStringLiteral("files")))};
      if (!validId.match(port.id).hasMatch() || ids.contains(port.id)) {
        problems.append(QStringLiteral("invalid or repeated id \"%1\"").arg(port.id));
        continue;
      }
      if (port.name.isEmpty() || port.game.isEmpty() || port.system.isEmpty() ||
          !port.repo.startsWith(QStringLiteral("https://"))) {
        problems.append(QStringLiteral("%1 needs a name, game, system and https repo").arg(port.id));
        continue;
      }
      bool lowercase = true;
      for (const QString& file : port.files) {
        lowercase = lowercase && file == file.toLower();
      }
      if (!lowercase) {
        problems.append(QStringLiteral("%1 has file names that are not lowercase").arg(port.id));
        continue;
      }
      if (!port.titles.contains(port.game)) {
        port.titles.prepend(port.game);
      }
      ids.insert(port.id);
      catalog.m_ports.append(port);
    }
  }
  if (error != nullptr) {
    *error = problems.join(QStringLiteral("; "));
  }
  return catalog;
}

const NativePort* PortCatalog::find(const QString& id) const {
  for (const NativePort& port : m_ports) {
    if (port.id == id) {
      return &port;
    }
  }
  return nullptr;
}

bool PortCatalog::fileMatches(const QString& fileName, const QString& token) {
  if (token.isEmpty()) {
    return false;
  }
  const QString name = fileName.toLower();
  for (qsizetype at = name.indexOf(token); at >= 0; at = name.indexOf(token, at + 1)) {
    const qsizetype end = at + token.size();
    if ((at == 0 || !isWordCharacter(name.at(at - 1))) &&
        (end == name.size() || !isWordCharacter(name.at(end)))) {
      return true;
    }
  }
  return false;
}

const NativePort* PortCatalog::matchFile(const QString& fileName) const {
  for (const NativePort& port : m_ports) {
    for (const QString& token : port.files) {
      if (fileMatches(fileName, token)) {
        return &port;
      }
    }
  }
  return nullptr;
}

QString PortCatalog::normalizedSerial(const QString& serial) {
  QString normalized;
  for (const QChar character : serial.toUpper()) {
    if (character.isLetterOrNumber()) {
      normalized.append(character);
    }
  }
  return normalized;
}

// "Legend of Zelda, The - Ocarina of Time (USA) [!]" and "The Legend of Zelda: Ocarina of
// Time" are the same game.
QString PortCatalog::normalizedTitle(const QString& title) {
  static const QRegularExpression tags(QStringLiteral("\\([^)]*\\)|\\[[^\\]]*\\]"));
  static const QRegularExpression article(QStringLiteral("\\bthe\\b"));
  static const QRegularExpression other(QStringLiteral("[^a-z0-9]+"));
  QString text = title.toCaseFolded();
  text.remove(tags);
  text.replace(QLatin1Char('&'), QStringLiteral(" and "));
  text.remove(article);
  text.remove(other);
  return text;
}

QVector<const NativePort*> PortCatalog::portsFor(const QString& system, const QString& serial,
                                                 const QString& title) const {
  QVector<const NativePort*> found;
  const QString wantedSerial = normalizedSerial(serial);
  const QString wantedTitle = normalizedTitle(title);
  for (const NativePort& port : m_ports) {
    if (port.system != system) {
      continue;
    }
    bool matched = false;
    for (const QString& known : port.serials) {
      matched = matched || (!wantedSerial.isEmpty() && normalizedSerial(known) == wantedSerial);
    }
    for (const QString& known : port.titles) {
      matched = matched || (!wantedTitle.isEmpty() && normalizedTitle(known) == wantedTitle);
    }
    if (matched) {
      found.append(&port);
    }
  }
  return found;
}
