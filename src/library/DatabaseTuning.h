#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>

// Opens a library database connection tuned for many small writes from the
// interface thread: write-ahead logging keeps readers unblocked, NORMAL sync
// skips the per-statement fsync that made each cover arrival a stall, and the
// busy timeout lets the models that share one file wait instead of failing.
inline bool openTunedDatabase(QSqlDatabase& database) {
  if (!database.open()) {
    return false;
  }
  QSqlQuery pragma(database);
  pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
  if (database.databaseName() != QStringLiteral(":memory:")) {
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
  }
  return true;
}
