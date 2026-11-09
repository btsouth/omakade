#pragma once

#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

namespace ReviewAvailability {

struct Entry {
  QString key;
  QString source;
  QString path;
  QString sourceError;
  QString route;
  QString mode;
  QString core;
  QString system;
  QString rommRoot;
  QVariantMap installation;
  QVariantMap setup;
  bool flatpak = false;
  bool preferStandaloneEmulators = false;
  bool emulator = false;
  bool manual = false;
  bool installedKnown = false;
  bool installed = true;
  // The source knows the game's library storage is disconnected (an unplugged Steam drive).
  bool storageUnavailable = false;
};

struct Plan {
  QString error;
  QString errorCategory;
};

struct Result {
  QString key;
  QStringList reasons;
  QVariantList reasonDetails;
  bool pathChecked = false;
  bool contentAvailable = true;
  bool launchable = true;
  bool operator==(const Result&) const = default;
};

struct Evaluation {
  QVector<Result> results;
  qint64 elapsedMs = 0;
  int entryCount = 0;
};

using ContentAvailable = std::function<bool(const QString&)>;
using MissingAncestor = std::function<QString(const QString&)>;
using PlanResolver = std::function<Plan(const Entry&)>;

QVector<Result> evaluate(const QVector<Entry>& entries, const ContentAvailable& contentAvailable,
                         const MissingAncestor& missingAncestor, const PlanResolver& resolvePlan);
QString firstMissingAncestor(const QString& path);
bool matchesFilter(const QString& filter, const QStringList& reasons);

} // namespace ReviewAvailability
