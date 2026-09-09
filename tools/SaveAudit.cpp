// Read-only by default. --snapshot-existing copies discovered saves, never restores them.
#include "launch/GameLauncher.h"
#include "saves/SaveBackups.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
  QJsonParseError error;
  const auto request = QJsonDocument::fromJson(input.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !request.isObject()) return 1;
  GameLauncher launcher;
  launcher.setPreferStandaloneEmulators(request.object().value("preferStandalone").toBool());
  SaveBackups backups;
  QJsonArray results;
  for (const auto& value : request.object().value("games").toArray()) {
    const auto game = value.toObject();
    if (game.value("flatpak").toBool()) continue;
    QString launchError;
    const QString path = game.value("path").toString();
    const auto command = launcher.plannedCartridgeCommand(path, game.value("core").toString(),
        false, game.value("system").toString(), &launchError);
    const int index = command.arguments.indexOf("-L");
    if (command.program != "retroarch" || index < 0 || index + 1 >= command.arguments.size()) continue;
    const QString core = command.arguments.at(index + 1);
    const QString save = backups.discover(path, core);
    if (save.isEmpty()) continue;
    const bool copying = app.arguments().contains("--snapshot-existing");
    const bool okay = !copying || backups.protect(path, core);
    results.append(QJsonObject{{"game", path}, {"save", save}, {"core", core},
                              {"okay", okay}, {"versions", backups.count(path)}});
  }
  QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
  output.write(QJsonDocument(results).toJson());
}
