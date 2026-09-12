#pragma once
#include <QJsonObject>
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
  bool valid() const { return error.isEmpty() && (!files.isEmpty() || !trees.isEmpty()); }
};

// Complete save sets with a persistent rollback journal. No emulator-specific paths here.
class SaveSetStore {
public:
  using Resolver = std::function<SaveLayout(const QJsonObject&)>;
  SaveSetStore(QString root, std::function<bool()> running);
  void setPolicy(int retention, qint64 bytes, const QString& budgetRoot = {});
  QVariantList versions(const QString& game) const;
  bool snapshot(const QString& game, const QJsonObject& context, const SaveLayout& layout,
                QString* error, bool allowEmpty = false);
  bool remove(const QString& game, const QString& version, QString* error);
  bool restore(const QString& game, const QString& version, const Resolver& resolve,
               QString* error);
  bool recover(const Resolver& resolve, QString* error);
  bool pending() const;

private:
  QString m_root, m_budgetRoot;
  int m_retention = 10;
  qint64 m_storageLimit = 2LL*1024*1024*1024;
  std::function<bool()> m_running;
};
