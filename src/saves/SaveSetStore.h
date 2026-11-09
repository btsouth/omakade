#pragma once
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVariantList>
#include <functional>

struct SaveLayout {
  QStringList files;
  QStringList trees;
  QString description;
  QString error;
  bool shared = false;
  QStringList patterns;    // Optional basename filters for trees containing other emulator data.
  QString relativePattern; // Optional anchored expression matching paths relative to a tree.
  bool allowEmptySnapshot = false; // A resolver may know that no save exists yet.
  // Resolver-defined save roles, never inferred from sorted path positions.
  QMap<QString, QString> relocationFiles;
  QMap<QString, QString> relocationTrees;
  bool valid() const {
    return error.isEmpty() &&
           (allowEmptySnapshot || !files.isEmpty() || !trees.isEmpty());
  }
};

// Complete save sets with a persistent rollback journal. No emulator-specific paths here.
class SaveSetStore {
public:
  using Resolver = std::function<SaveLayout(const QJsonObject&)>;
  using CopyFile = std::function<bool(const QString&, const QString&)>;
  SaveSetStore(QString root, std::function<bool()> running);
  void setPolicy(int retention, qint64 bytes, const QString& budgetRoot = {});
  QVariantList versions(const QString& game) const;
  bool stageGameCopy(const QString& oldGame, const QString& newGame, const Resolver& resolve,
                     const QString& destination, const CopyFile& copyFile,
                     QStringList* copiedVersions, QString* error) const;
  bool validateGameCopies(const QString& game, const QStringList& versions,
                           const Resolver& resolve, QString* error) const;
  bool snapshot(const QString& game, const QJsonObject& context, const SaveLayout& layout,
                QString* error, bool allowEmpty = false);
  bool remove(const QString& game, const QString& version, QString* error);
  bool restore(const QString& game, const QString& version, const Resolver& resolve,
               QString* error);
  bool recover(const Resolver& resolve, QString* error);
  bool pending() const;
  // True when an unresolved journal may touch this game's saves, or cannot be read.
  bool pendingAffects(const QString& game, const SaveLayout& layout) const;
  // Keeps the live saves and moves the journal to a dated folder instead of deleting it.
  bool archiveRecovery(QString* folder, QString* error);

private:
  QString m_root, m_budgetRoot;
  int m_retention = 10;
  qint64 m_storageLimit = 2LL*1024*1024*1024;
  std::function<bool()> m_running;
};
