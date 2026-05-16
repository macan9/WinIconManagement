#include "Persistence/RestoreSessionRepository.h"

#include <winsqlite/winsqlite3.h>

namespace Persistence {
RestoreSessionRepository::RestoreSessionRepository(Database* database) : database_(database) {}

std::optional<RestoreSessionRecord> RestoreSessionRepository::Load() {
    if (database_ == nullptr || !database_->IsOpen()) {
        return std::nullopt;
    }

    Statement statement;
    if (!database_->Prepare(
            "SELECT id, last_exit_mode, last_shutdown_clean, last_restore_needed, updated_at_utc "
            "FROM RestoreSessions WHERE id=1 LIMIT 1;",
            &statement)) {
        return std::nullopt;
    }

    if (statement.Step() != SQLITE_ROW) {
        return std::nullopt;
    }

    RestoreSessionRecord record{};
    record.id = statement.ColumnInt64(0);
    record.lastExitMode = statement.ColumnText(1);
    record.lastShutdownClean = statement.ColumnInt(2) != 0;
    record.lastRestoreNeeded = statement.ColumnInt(3) != 0;
    record.updatedAtUtc = statement.ColumnText(4);
    return record;
}

bool RestoreSessionRepository::Save(const RestoreSessionRecord& record) {
    if (database_ == nullptr || !database_->IsOpen()) {
        return false;
    }

    Statement statement;
    if (!database_->Prepare(
            "INSERT INTO RestoreSessions(id, last_exit_mode, last_shutdown_clean, last_restore_needed, updated_at_utc) "
            "VALUES(?1, ?2, ?3, ?4, ?5) "
            "ON CONFLICT(id) DO UPDATE SET "
            "last_exit_mode=excluded.last_exit_mode, "
            "last_shutdown_clean=excluded.last_shutdown_clean, "
            "last_restore_needed=excluded.last_restore_needed, "
            "updated_at_utc=excluded.updated_at_utc;",
            &statement)) {
        return false;
    }

    return statement.BindInt64(1, record.id <= 0 ? 1 : record.id) &&
           statement.BindText(2, record.lastExitMode.empty() ? std::wstring(L"unknown") : record.lastExitMode) &&
           statement.BindInt(3, record.lastShutdownClean ? 1 : 0) &&
           statement.BindInt(4, record.lastRestoreNeeded ? 1 : 0) &&
           statement.BindText(5, record.updatedAtUtc.empty() ? UtcNowIso8601() : record.updatedAtUtc) &&
           statement.Step() == SQLITE_DONE;
}
}  // namespace Persistence
