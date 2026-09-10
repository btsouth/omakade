// Read-only unless --snapshot-existing is supplied. Never restores live saves.
#include "launch/GameLauncher.h"
#include "saves/SaveBackups.h"
#include "saves/SaveLayouts.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile in;
  if (!in.open(stdin, QIODevice::ReadOnly))
    return 1;
  const auto request = QJsonDocument::fromJson(in.readAll());
  if (!request.isArray())
    return 1;
  SaveBackups backups;
  GameLauncher launcher;
  QJsonArray rows;
  const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                         "/retroarch/retroarch.cfg";
  for (const auto& value : request.array()) {
    auto c = value.toObject();
    QString launchError;
    if (c["source"].toString() == "RetroArch") {
      auto command = launcher.plannedCartridgeCommand(c["game"].toString(), c["core"].toString(),
                                                      c["flatpak"].toBool(), c["system"].toString(),
                                                      &launchError);
      const int i = command.arguments.indexOf("-L");
      if (i >= 0 && i + 1 < command.arguments.size())
        c["core"] = command.arguments[i + 1];
      else if (command.program != "flatpak")
        c["source"] = command.program;
    }
    const auto layout = resolveSaveLayout(c, QDir::homePath(), config);
    QStringList files = layout.files;
    for (const auto& tree : layout.trees) {
      QDirIterator it(tree, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) {
        const auto p = it.next();
        if ((layout.patterns.isEmpty() || QDir::match(layout.patterns, QFileInfo(p).fileName())) &&
            (layout.relativePattern.isEmpty() || QRegularExpression(layout.relativePattern)
                                                     .match(QDir(tree).relativeFilePath(p))
                                                     .hasMatch()))
          files << p;
      }
    }
    files.removeDuplicates();
    QStringList existing;
    qint64 bytes = 0;
    for (const auto& p : files)
      if (QFileInfo(p).isFile()) {
        existing << p;
        bytes += QFileInfo(p).size();
      }
    bool okay = layout.valid();
    if (okay && !existing.isEmpty() && app.arguments().contains("--snapshot-existing"))
      okay = backups.protectLaunch(c["source"].toString(), c["game"].toString(),
                                   c["core"].toString(), c["flatpak"].toBool(), c["id"].toString(),
                                   c["runner"].toString(), c["target"].toString());
    rows << QJsonObject{{"context", c},
                        {"supported", layout.valid()},
                        {"error", layout.error},
                        {"shared", layout.shared},
                        {"files", QJsonArray::fromStringList(existing)},
                        {"bytes", bytes},
                        {"okay", okay},
                        {"versions", backups.count(c["game"].toString())}};
  }
  QFile out;
  if (!out.open(stdout, QIODevice::WriteOnly))
    return 1;
  const auto bytes = QJsonDocument(rows).toJson();
  return out.write(bytes) == bytes.size() ? 0 : 1;
}
