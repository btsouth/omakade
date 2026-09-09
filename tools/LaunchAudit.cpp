// Resolve supplied ROM records using production code, without opening a library,
// starting an emulator, or changing its configuration. Reads JSON from stdin.
#include "launch/GameLauncher.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile input;
  if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(input.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) return 1;
  GameLauncher launcher;
  launcher.setPreferStandaloneEmulators(document.object().value("preferStandalone").toBool());
  QJsonArray results;
  for (const auto& item : document.object().value("games").toArray()) {
    const auto game = item.toObject();
    QString error;
    const auto command = launcher.plannedCartridgeCommand(game.value("path").toString(),
        game.value("core").toString(), game.value("flatpak").toBool(),
        game.value("system").toString(), &error);
    results.append(QJsonObject{{"id", game.value("id")}, {"title", game.value("title")},
        {"system", GameLauncher::cartridgeSystem(game.value("path").toString(), game.value("system").toString())},
        {"program", command.program}, {"arguments", QJsonArray::fromStringList(command.arguments)},
        {"error", error}});
  }
  QFile output;
  if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
  output.write(QJsonDocument(results).toJson());
}
