#include "Persistence/SettingsRepository.h"

#include <winsqlite/winsqlite3.h>

namespace Persistence {
SettingsRepository::SettingsRepository(Database* database) : database_(database) {}

bool SettingsRepository::Upsert(const std::wstring& key, const std::wstring& value) {
    if (database_ == nullptr || !database_->IsOpen()) {
        return false;
    }

    Statement statement;
    if (!database_->Prepare(
            "INSERT INTO AppSettings(key, value, updated_at_utc) "
            "VALUES(?1, ?2, ?3) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at_utc=excluded.updated_at_utc;",
            &statement)) {
        return false;
    }

    if (!statement.BindText(1, key) ||
        !statement.BindText(2, value) ||
        !statement.BindText(3, UtcNowIso8601())) {
        return false;
    }

    return statement.Step() == SQLITE_DONE;
}

bool SettingsRepository::TryGet(const std::wstring& key, std::wstring* outValue) {
    if (database_ == nullptr || !database_->IsOpen() || outValue == nullptr) {
        return false;
    }

    Statement statement;
    if (!database_->Prepare(
            "SELECT value FROM AppSettings WHERE key=?1 LIMIT 1;",
            &statement)) {
        return false;
    }
    if (!statement.BindText(1, key)) {
        return false;
    }

    const int stepResult = statement.Step();
    if (stepResult != SQLITE_ROW) {
        return false;
    }
    *outValue = statement.ColumnText(0);
    return true;
}

std::vector<AppSettingRecord> SettingsRepository::ListAll() {
    std::vector<AppSettingRecord> items;
    if (database_ == nullptr || !database_->IsOpen()) {
        return items;
    }

    Statement statement;
    if (!database_->Prepare(
            "SELECT key, value, updated_at_utc FROM AppSettings ORDER BY key ASC;",
            &statement)) {
        return items;
    }

    while (true) {
        const int stepResult = statement.Step();
        if (stepResult == SQLITE_DONE) {
            break;
        }
        if (stepResult != SQLITE_ROW) {
            break;
        }

        AppSettingRecord item{};
        item.key = statement.ColumnText(0);
        item.value = statement.ColumnText(1);
        item.updatedAtUtc = statement.ColumnText(2);
        items.push_back(std::move(item));
    }
    return items;
}
}  // namespace Persistence

