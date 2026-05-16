#include "Persistence/Schema.h"

#include <array>
#include <span>
#include <string_view>

namespace Persistence {
namespace {
constexpr std::array<std::string_view, 6> kSchemaV1Sql = {
    "CREATE TABLE IF NOT EXISTS AppSettings ("
    "  key TEXT PRIMARY KEY,"
    "  value TEXT NOT NULL,"
    "  updated_at_utc TEXT NOT NULL"
    ");",

    "CREATE TABLE IF NOT EXISTS Fences ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL,"
    "  left_px INTEGER NOT NULL,"
    "  top_px INTEGER NOT NULL,"
    "  width_px INTEGER NOT NULL,"
    "  height_px INTEGER NOT NULL,"
    "  style_json TEXT NOT NULL DEFAULT '{}',"
    "  created_at_utc TEXT NOT NULL,"
    "  updated_at_utc TEXT NOT NULL"
    ");",

    "CREATE TABLE IF NOT EXISTS FenceIcons ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  fence_id INTEGER NOT NULL,"
    "  icon_identity TEXT NOT NULL,"
    "  icon_name TEXT NOT NULL DEFAULT '',"
    "  order_index INTEGER NOT NULL DEFAULT 0,"
    "  current_x INTEGER NOT NULL,"
    "  current_y INTEGER NOT NULL,"
    "  original_x INTEGER NOT NULL,"
    "  original_y INTEGER NOT NULL,"
    "  updated_at_utc TEXT NOT NULL,"
    "  FOREIGN KEY(fence_id) REFERENCES Fences(id) ON DELETE CASCADE"
    ");",

    "CREATE TABLE IF NOT EXISTS Snapshots ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL,"
    "  source TEXT NOT NULL DEFAULT 'manual',"
    "  created_at_utc TEXT NOT NULL"
    ");",

    "CREATE TABLE IF NOT EXISTS SnapshotIcons ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  snapshot_id INTEGER NOT NULL,"
    "  icon_identity TEXT NOT NULL,"
    "  icon_name TEXT NOT NULL DEFAULT '',"
    "  icon_index INTEGER NOT NULL,"
    "  x INTEGER NOT NULL,"
    "  y INTEGER NOT NULL,"
    "  FOREIGN KEY(snapshot_id) REFERENCES Snapshots(id) ON DELETE CASCADE"
    ");",

    "CREATE INDEX IF NOT EXISTS idx_snapshot_icons_snapshot_id "
    "ON SnapshotIcons(snapshot_id);"};

constexpr std::array<std::string_view, 1> kSchemaV2Sql = {
    "CREATE TABLE IF NOT EXISTS RestoreSessions ("
    "  id INTEGER PRIMARY KEY CHECK(id = 1),"
    "  last_exit_mode TEXT NOT NULL DEFAULT 'unknown',"
    "  last_shutdown_clean INTEGER NOT NULL DEFAULT 1,"
    "  last_restore_needed INTEGER NOT NULL DEFAULT 0,"
    "  updated_at_utc TEXT NOT NULL"
    ");"};

bool ApplyStatements(Database& database, std::span<const std::string_view> statements) {
    for (const std::string_view sql : statements) {
        if (!database.Execute(sql)) {
            return false;
        }
    }
    return true;
}
}

bool EnsureSchema(Database& database) {
    if (!database.IsOpen()) {
        return false;
    }

    const int currentVersion = database.GetUserVersion();
    if (currentVersion < 0) {
        return false;
    }
    if (currentVersion >= kDatabaseSchemaVersion) {
        return true;
    }

    if (!database.BeginTransaction()) {
        return false;
    }

    if (currentVersion < 1) {
        if (!ApplyStatements(database, kSchemaV1Sql) ||
            !database.SetUserVersion(1)) {
            const bool rolledBack = database.RollbackTransaction();
            (void)rolledBack;
            return false;
        }
    }

    if (currentVersion < 2) {
        if (!ApplyStatements(database, kSchemaV2Sql) ||
            !database.SetUserVersion(2)) {
            const bool rolledBack = database.RollbackTransaction();
            (void)rolledBack;
            return false;
        }
    }

    if (!database.CommitTransaction()) {
        const bool rolledBack = database.RollbackTransaction();
        (void)rolledBack;
        return false;
    }
    return true;
}
}  // namespace Persistence
