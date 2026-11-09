#pragma once
#include "backup/BackupArchive.h"

class BackupSnapshot {
public:
  // Read one consistent SQLite snapshot. Old source flags are converted to the
  // portable override table without modifying the source database.
  static bool capture(const QString& databasePath, const QJsonObject& settings,
                      BackupPayload* payload, QString* error = nullptr);
  // True when the file exists but SQLite cannot read it or it fails quick_check.
  // A busy or locked database, or one with a hot rollback journal, is not damaged.
  static bool damaged(const QString& databasePath);
};
