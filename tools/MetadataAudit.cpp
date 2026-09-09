// Offline adapter for comparing provider snapshots with the production matching rules.
// Reads JSON on stdin and writes JSON on stdout. It never opens the user's database.
#include "metadata/GameMetadata.h"
#include "library/RetroArchGameModel.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile input;
  if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(input.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return 1;
  const auto request = document.object();
  QJsonObject output;
  output["fingerprint"] = QString::fromLatin1(GameMetadata::matchingRulesFingerprint());
  if (request.contains("normalize")) {
    QJsonArray names;
    for (const auto& name : request.value("normalize").toArray())
      names.append(GameMetadata::normalizedTitle(name.toString()));
    output["normalize"] = names;
  }
  if (request.contains("queries")) {
    QJsonArray queries;
    for (const auto& value : request.value("queries").toArray()) {
      const auto row = value.toObject();
      const auto title = row.value("title").toString();
      const auto system = row.value("system").toString();
      queries.append(QJsonObject{{"title", title},
          {"normalized", GameMetadata::normalizedTitle(title)},
          {"search", QString::fromUtf8(GameMetadata::searchQuery(title, system))},
          {"aliases", QString::fromUtf8(GameMetadata::aliasSearchQuery(title, system))},
          {"discovery", QString::fromUtf8(GameMetadata::discoveryQuery(title, system))}});
    }
    output["queries"] = queries;
  }
  if (request.contains("equivalent")) {
    QJsonArray results;
    for (const auto& value : request.value("equivalent").toArray()) {
      const auto pair = value.toArray();
      results.append(pair.size() == 2 && GameMetadata::equivalentTitle(pair[0].toString(), pair[1].toString()));
    }
    output["equivalent"] = results;
  }
  if (request.contains("covers")) {
    QJsonArray labels;
    for (const auto& value : request.value("covers").toArray()) {
      const auto row = value.toObject();
      labels.append(QJsonArray::fromStringList(RetroArchGameModel::coverLabelCandidates(
          row.value("title").toString(), row.value("fileBase").toString())));
    }
    output["covers"] = labels;
  }
  if (request.contains("grids")) {
    QJsonArray matches;
    for (const auto& value : request.value("grids").toArray()) {
      const auto row = value.toObject();
      matches.append(GameMetadata::chooseGridMatch(row.value("candidates").toArray().toVariantList(),
          row.value("title").toString(), row.value("year").toInt(), row.value("system").toString()));
    }
    output["grids"] = matches;
  }
  QFile out;
  if (!out.open(stdout, QIODevice::WriteOnly)) return 1;
  out.write(QJsonDocument(output).toJson(QJsonDocument::Compact));
}
