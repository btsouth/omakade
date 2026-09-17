#include "backup/BackupDatabase.h"
#include "tracking/SessionDatabase.h"
#include <QLockFile>
#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

namespace {
const QMap<QString, QString> schemas{
    {"play_sessions",
     "id INTEGER PRIMARY KEY, session_key TEXT UNIQUE, game_path TEXT NOT NULL, "
     "source TEXT NOT NULL DEFAULT '', started_at INTEGER NOT NULL, ended_at INTEGER NOT NULL "
     "DEFAULT 0, "
     "seconds INTEGER NOT NULL DEFAULT 0, pid INTEGER NOT NULL DEFAULT 0, "
     "proc_start INTEGER NOT NULL DEFAULT -1, heartbeat_at INTEGER NOT NULL DEFAULT 0"},
    {"play_baselines", "game_path TEXT PRIMARY KEY, baseline_seconds INTEGER NOT NULL DEFAULT 0, "
                       "captured_at INTEGER NOT NULL, schema INTEGER NOT NULL DEFAULT 1"},
    {"game_metadata", "game_key TEXT PRIMARY KEY, payload TEXT NOT NULL"},
    {"user_game_flags", "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, "
                        "favorite INTEGER, hidden INTEGER, PRIMARY KEY(source,runner,app_id)"},
    {"game_organization",
     "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, completion_status TEXT NOT "
     "NULL DEFAULT '', tags_json TEXT NOT NULL DEFAULT '[]', pinned INTEGER NOT NULL DEFAULT 0, "
     "PRIMARY KEY(source,runner,app_id)"},
    {"collections", "name TEXT PRIMARY KEY COLLATE NOCASE, created_at INTEGER NOT NULL"},
    {"collection_games",
     "collection_name TEXT NOT NULL, source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT "
     "NULL, PRIMARY KEY(collection_name,source,runner,app_id)"},
    {"game_link_members",
     "group_id TEXT NOT NULL, source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, "
     "is_primary INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(source,runner,app_id)"},
    {"launch_preferences",
     "group_id TEXT PRIMARY KEY, source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL"},
    {"launch_activity",
     "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, last_launched INTEGER NOT "
     "NULL, launch_count INTEGER NOT NULL DEFAULT 1, PRIMARY KEY(source,runner,app_id)"},
    {"manual_games", "id TEXT PRIMARY KEY, entry TEXT NOT NULL, favorite INTEGER NOT NULL DEFAULT "
                     "0, hidden INTEGER NOT NULL DEFAULT 0, active INTEGER NOT NULL DEFAULT 1"},
    {"play_queue", "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, title TEXT "
                   "NOT NULL, position INTEGER NOT NULL, PRIMARY KEY(source,runner,app_id)"},
    {"saved_filters", "id TEXT PRIMARY KEY, name TEXT NOT NULL, name_key TEXT NOT NULL UNIQUE, "
                      "state_json TEXT NOT NULL"},
    {"artwork_overrides", "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, "
                          "cover_path TEXT NOT NULL, hero_path TEXT NOT NULL DEFAULT '', logo_path "
                          "TEXT NOT NULL DEFAULT '', PRIMARY KEY(source,runner,app_id)"}};
QStringList primaryKey(const QString& table) {
  if (table == "game_metadata")
    return {"game_key"};
  if (table == "play_sessions")
    return {"session_key"};
  if (table == "play_baselines")
    return {"game_path"};
  if (table == "collections")
    return {"name"};
  if (table == "manual_games" || table == "saved_filters")
    return {"id"};
  if (table == "launch_preferences")
    return {"group_id"};
  if (table == "collection_games")
    return {"collection_name", "source", "runner", "app_id"};
  return {"source", "runner", "app_id"};
}
bool stageArtwork(const QString& directory, const BackupPayload& payload, QString* error) {
  const auto fail = [&](const QString& message) {
    if (error)
      *error = message;
    return false;
  };
  if (payload.artwork.isEmpty())
    return true;
  if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory))
    return fail("Could not create the owned artwork folder.");
  for (auto asset = payload.artwork.begin(); asset != payload.artwork.end(); ++asset) {
    const QString path = directory + "/" + asset.key().mid(QStringLiteral("artwork/").size());
    const QFileInfo existing(path);
    if (existing.isSymLink() || (existing.exists() && !existing.isFile()))
      return fail("An artwork destination is not a regular file.");
    QFile old(path);
    if (old.open(QIODevice::ReadOnly) && old.size() <= BackupArchive::MaxImageBytes &&
        old.readAll() == asset.value())
      continue;
    old.close();
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
      return fail("Could not stage restored artwork.");
    output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (output.write(asset.value()) != asset.value().size() || !output.commit())
      return fail("Could not save complete restored artwork.");
  }
  return true;
}
bool restoreDatabase(QSqlDatabase& database, const QString& artworkDirectory,
                     const BackupPayload& payload, BackupDatabase::Mode mode, QString* error) {
  const auto fail = [&](const QString& message) {
    database.rollback();
    if (error)
      *error = message;
    return false;
  };
  if (!database.transaction())
    return fail("Could not start the restore transaction.");
  QSqlQuery query(database);
  for (auto schema = schemas.begin(); schema != schemas.end(); ++schema)
    if (!query.exec("CREATE TABLE IF NOT EXISTS " + schema.key() + " (" + schema.value() + ")"))
      return fail("Could not prepare the personal-data schema.");
  if (payload.library.contains("play_sessions") && !SessionDatabase::ensureSchema(database))
    return fail("Could not prepare portable play history.");
  if (!query.exec("PRAGMA table_info(artwork_overrides)"))
    return fail("Could not inspect the artwork schema.");
  QSet<QString> artworkColumns;
  while (query.next())
    artworkColumns.insert(query.value(1).toString());
  for (const auto& column : {QStringLiteral("hero_path"), QStringLiteral("logo_path")})
    if (!artworkColumns.contains(column) &&
        !query.exec("ALTER TABLE artwork_overrides ADD COLUMN " + column +
                    " TEXT NOT NULL DEFAULT ''"))
      return fail("Could not migrate the artwork schema.");

  if (!query.exec("PRAGMA table_info(game_organization)"))
    return fail("Could not inspect the organization schema.");
  bool hasPinned = false;
  while (query.next())
    hasPinned = hasPinned || query.value(1).toString() == "pinned";
  query.finish();
  if (!hasPinned && !query.exec("ALTER TABLE game_organization ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0"))
    return fail("Could not migrate console pins.");

  if (mode == BackupDatabase::Mode::Replace) {
    for (auto schema = schemas.begin(); schema != schemas.end(); ++schema)
      if (((schema.key() != "game_metadata" && schema.key() != "play_sessions" &&
            schema.key() != "play_baselines" && schema.key() != "play_queue") ||
           payload.library.contains(schema.key())) &&
          !query.exec("DELETE FROM " + schema.key()))
        return fail("Could not replace existing personal records.");
    if (!query.exec("SELECT name FROM sqlite_master WHERE type='table'"))
      return fail("Could not inspect cached sources.");
    QSet<QString> tables;
    while (query.next())
      tables.insert(query.value(0).toString());
    for (const QString& table :
         {QStringLiteral("games"), QStringLiteral("lutris_games"), QStringLiteral("heroic_games"),
          QStringLiteral("faugus_games"), QStringLiteral("retroarch_games"),
          QStringLiteral("pcsx2_games"), QStringLiteral("ryujinx_games"),
          QStringLiteral("battlenet_games"), QStringLiteral("dolphin_games"),
          QStringLiteral("cemu_games"), QStringLiteral("xenia_games"), QStringLiteral("shadps4_games")})
      if (tables.contains(table) && !query.exec("UPDATE " + table + " SET favorite=0, hidden=0"))
        return fail("Could not reset legacy personal flags.");
  }

  const QJsonArray incomingLinks = payload.library.value("game_link_members").toArray();
  QSet<QString> incomingGroups, affectedGroups;
  for (const auto& value : incomingLinks)
    incomingGroups.insert(value.toObject().value("group_id").toString());
  for (const auto& value : incomingLinks) {
    const auto row = value.toObject();
    query.prepare(
        "SELECT group_id FROM game_link_members WHERE source=? AND runner=? AND app_id=?");
    query.addBindValue(row.value("source").toString());
    query.addBindValue(row.value("runner").toString());
    query.addBindValue(row.value("app_id").toString());
    if (!query.exec())
      return fail("Could not inspect linked-game conflicts.");
    while (query.next())
      affectedGroups.insert(query.value(0).toString());
    query.prepare("DELETE FROM game_link_members WHERE source=? AND runner=? AND app_id=?");
    query.addBindValue(row.value("source").toString());
    query.addBindValue(row.value("runner").toString());
    query.addBindValue(row.value("app_id").toString());
    if (!query.exec())
      return fail("Could not reconcile linked-game membership.");
  }
  // An incoming group's archived membership is authoritative. Other groups keep
  // their remaining members when at least two remain, with one valid primary.
  for (const auto& group : incomingGroups) {
    query.prepare("DELETE FROM game_link_members WHERE group_id=?");
    query.addBindValue(group);
    if (!query.exec())
      return fail("Could not replace an imported linked group.");
    query.prepare("DELETE FROM launch_preferences WHERE group_id=?");
    query.addBindValue(group);
    if (!query.exec())
      return fail("Could not reconcile a launch preference.");
  }
  for (const auto& group : affectedGroups - incomingGroups) {
    query.prepare("SELECT source, runner, app_id, is_primary FROM game_link_members WHERE "
                  "group_id=? ORDER BY is_primary DESC, source, runner, app_id");
    query.addBindValue(group);
    if (!query.exec())
      return fail("Could not inspect remaining linked installations.");
    QList<QStringList> members;
    while (query.next())
      members.append(
          {query.value(0).toString(), query.value(1).toString(), query.value(2).toString()});
    if (members.size() < 2) {
      query.prepare("DELETE FROM game_link_members WHERE group_id=?");
      query.addBindValue(group);
      if (!query.exec())
        return fail("Could not unlink an incomplete group.");
      query.prepare("DELETE FROM launch_preferences WHERE group_id=?");
      query.addBindValue(group);
      if (!query.exec())
        return fail("Could not remove an incomplete preference.");
    } else {
      const auto primary = members.first();
      query.prepare("UPDATE game_link_members SET is_primary=(source=? AND runner=? AND app_id=?) "
                    "WHERE group_id=?");
      for (const auto& part : primary)
        query.addBindValue(part);
      query.addBindValue(group);
      if (!query.exec())
        return fail("Could not retain a group's primary installation.");
      query.prepare("DELETE FROM launch_preferences WHERE group_id=? AND NOT EXISTS (SELECT 1 FROM "
                    "game_link_members m WHERE m.group_id=launch_preferences.group_id AND "
                    "m.source=launch_preferences.source AND m.runner=launch_preferences.runner AND "
                    "m.app_id=launch_preferences.app_id)");
      query.addBindValue(group);
      if (!query.exec())
        return fail("Could not reconcile a removed preferred installation.");
    }
  }

  QHash<QString, QString> collections;
  if (!query.exec("SELECT name FROM collections"))
    return fail("Could not inspect collections.");
  while (query.next())
    collections.insert(query.value(0).toString().toCaseFolded(), query.value(0).toString());
  // Matching IDs are replaced together, allowing archived names to swap without
  // spurious conflicts with records that are themselves about to be replaced.
  for (const auto& value : payload.library.value("saved_filters").toArray()) {
    query.prepare("DELETE FROM saved_filters WHERE id=?");
    query.addBindValue(value.toObject().value("id").toString());
    if (!query.exec())
      return fail("Could not reconcile saved-filter identities.");
  }
  QHash<QString, QString> filterOwner;
  if (!query.exec("SELECT id, name_key FROM saved_filters"))
    return fail("Could not inspect saved filters.");
  while (query.next())
    filterOwner.insert(query.value(1).toString(), query.value(0).toString());
  QSet<QString> existingHistory;
  if (mode == BackupDatabase::Mode::Merge && payload.library.contains("play_sessions")) {
    if (!query.exec(
            "SELECT game_path FROM play_sessions UNION SELECT game_path FROM play_baselines"))
      return fail("Could not inspect existing play history.");
    while (query.next())
      existingHistory.insert(query.value(0).toString());
    query.finish();
  }
  const auto columns = BackupArchive::tableColumns();
  const QStringList order{"collections",        "user_game_flags",   "game_organization",
                          "manual_games",       "artwork_overrides", "launch_activity",
                          "saved_filters",      "collection_games",  "game_link_members",
                          "launch_preferences", "game_metadata",     "play_sessions",
                          "play_baselines",     "play_queue"};
  for (const auto& table : order) {
    const auto key = primaryKey(table);
    const auto fields = columns.value(table);
    QStringList placeholders, assignments;
    for (const auto& column : fields) {
      placeholders.append("?");
      if (!key.contains(column))
        assignments.append(column + "=" +
                           (table == "user_game_flags"
                                ? "COALESCE(excluded." + column + ",user_game_flags." + column + ")"
                                : "excluded." + column));
    }
    const QString suffix = table == "collections" || table == "collection_games"
                               ? " DO NOTHING"
                               : " DO UPDATE SET " + assignments.join(", ");
    const QString sql = "INSERT INTO " + table + "(" + fields.join(",") + ") VALUES(" +
                        placeholders.join(",") + ") ON CONFLICT(" + key.join(",") + ")" + suffix;
    auto incomingRows = payload.library.value(table).toArray().toVariantList();
    if (table == "play_queue")
      std::stable_sort(incomingRows.begin(), incomingRows.end(),
                       [](const QVariant& a, const QVariant& b) {
                         return a.toMap().value("position").toLongLong() <
                                b.toMap().value("position").toLongLong();
                       });
    for (const auto& value : incomingRows) {
      auto row = QJsonObject::fromVariantMap(value.toMap());
      if (table == "play_queue" && mode == BackupDatabase::Mode::Merge) {
        query.prepare("SELECT 1 FROM play_queue WHERE source=? AND runner=? AND app_id=?");
        for (const auto* field : {"source", "runner", "app_id"})
          query.addBindValue(row.value(field).toString());
        if (!query.exec())
          return fail("Could not inspect Up next.");
        if (query.next())
          continue;
        if (!query.exec("SELECT COUNT(*),COALESCE(MAX(position),-1)+1 FROM play_queue") ||
            !query.next())
          return fail("Could not inspect Up next order.");
        if (query.value(0).toInt() >= 100)
          return fail("Merged Up next would exceed 100 games.");
        row["position"] = query.value(1).toLongLong();
        query.finish();
      }
      if ((table == "play_sessions" || table == "play_baselines") &&
          existingHistory.contains(row.value("game_path").toString()))
        continue;
      if (table == "play_sessions") {
        query.prepare("SELECT game_path FROM play_sessions WHERE session_key=?");
        query.addBindValue(row.value("session_key").toString());
        if (!query.exec())
          return fail("Could not check the play session identity.");
        if (query.next() && query.value(0).toString() != row.value("game_path").toString())
          return fail("A play session identity belongs to a different game path.");
        query.finish();
      }
      if (table == "collections" || table == "collection_games") {
        const QString field = table == "collections" ? "name" : "collection_name";
        const QString name = row.value(field).toString();
        const QString canonical = collections.value(name.toCaseFolded(), name);
        collections.insert(name.toCaseFolded(), canonical);
        row.insert(field, canonical);
      }
      if (table == "saved_filters") {
        const QString id = row.value("id").toString();
        const QString originalName = row.value("name").toString();
        QString name = originalName;
        QString nameKey = name.normalized(QString::NormalizationForm_C).toCaseFolded();
        const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex().left(8));
        int attempt = 0;
        while (filterOwner.contains(nameKey) && filterOwner.value(nameKey) != id) {
          if (++attempt > 501)
            return fail("Could not choose a unique restored filter name.");
          name = originalName.left(70) + " (restored " + digest +
                 (attempt > 1 ? "-" + QString::number(attempt) : QString{}) + ")";
          nameKey = name.normalized(QString::NormalizationForm_C).toCaseFolded();
        }
        for (auto owner = filterOwner.begin(); owner != filterOwner.end();) {
          if (owner.value() == id)
            owner = filterOwner.erase(owner);
          else
            ++owner;
        }
        filterOwner.insert(nameKey, id);
        if (filterOwner.size() > 500)
          return fail("Merge would exceed the 500 saved-filter limit. Remove some views or choose "
                      "replacement.");
        row.insert("name", name);
        row.insert("name_key", nameKey);
      }
      // Earlier archives have no pin choice. Merge leaves a local pin intact;
      // replacement and new rows use the unpinned default.
      if (table == "game_organization" && !row.contains("pinned")) {
        bool pinned = false;
        if (mode == BackupDatabase::Mode::Merge) {
          query.prepare("SELECT pinned FROM game_organization WHERE source=? AND runner=? AND app_id=?");
          for (const auto& field : {"source", "runner", "app_id"})
            query.addBindValue(row.value(field).toString());
          if (!query.exec())
            return fail("Could not inspect an existing console pin.");
          if (query.next())
            pinned = query.value(0).toBool();
          query.finish();
        }
        row.insert("pinned", pinned);
      }
      // An archive written before the recorded-time watermark existed carries none.
      // Bind the column default so the row lands as unobserved, and the schema
      // migration regenerates the watermark on the next open exactly as it does for a
      // database from the previous release. Duplicating that substitution here would
      // be a second implementation of the same rule to keep in step.
      if (table == "play_baselines" && !row.contains("imported_seconds")) {
        row.insert("imported_seconds", -1);
        row.insert("observed_seconds", -1);
      }
      query.prepare(sql);
      for (const auto& column : fields) {
        auto field = row.value(column);
        if (table == "artwork_overrides" && column.endsWith("_path") && !field.toString().isEmpty())
          field = artworkDirectory + "/" + field.toString().mid(QStringLiteral("artwork/").size());
        query.addBindValue(field.isNull() ? QVariant{} : field.toVariant());
      }
      if (!query.exec())
        return fail("Could not import personal records into " + table +
                    ". No database changes were committed.");
    }
  }
  if (!database.commit())
    return fail("Could not commit restored personal records.");
  return true;
}
} // namespace

bool BackupDatabase::restore(const QString& path, const BackupPayload& payload, Mode mode,
                             QString* error) {
  if (mode != Mode::Merge && mode != Mode::Replace) {
    if (error)
      *error = "The restore mode is invalid.";
    return false;
  }
  if (!BackupArchive::validate(payload, error))
    return false;
  const QFileInfo file(path);
  if (!file.isAbsolute() || file.isSymLink() || !QFileInfo(file.absolutePath()).isDir()) {
    if (error)
      *error = "The restore database path is invalid.";
    return false;
  }
  QLockFile recorder(path + ".sessiond.lock");
  recorder.setStaleLockTime(0);
  if (payload.library.contains("play_sessions") && !recorder.tryLock(0)) {
    if (error)
      *error = "Stop the play-session recorder before restoring play history, then retry.";
    return false;
  }
  const QString artwork = file.absolutePath() + "/artwork";
  if (!stageArtwork(artwork, payload, error))
    return false;
  const QString connection =
      "omakade-restore-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  bool okay = false;
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
    database.setDatabaseName(path);
    database.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    if (database.open())
      okay = restoreDatabase(database, artwork, payload, mode, error);
    else if (error)
      *error = "Could not open the restore database.";
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
  return okay;
}
