#include "Persistence/FenceRepository.h"

#include <winsqlite/winsqlite3.h>

namespace Persistence {
FenceRepository::FenceRepository(Database* database) : database_(database) {}

long long FenceRepository::CreateFence(const FenceRecord& fence) {
    if (database_ == nullptr || !database_->IsOpen()) {
        return 0;
    }

    Statement insertFence;
    if (!database_->Prepare(
            "INSERT INTO Fences(name, left_px, top_px, width_px, height_px, style_json, created_at_utc, updated_at_utc) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);",
            &insertFence)) {
        return 0;
    }

    const int width = fence.bounds.right - fence.bounds.left;
    const int height = fence.bounds.bottom - fence.bounds.top;
    const std::wstring now = UtcNowIso8601();
    if (!insertFence.BindText(1, fence.name.empty() ? std::wstring(L"Desktop Group") : fence.name) ||
        !insertFence.BindInt(2, fence.bounds.left) ||
        !insertFence.BindInt(3, fence.bounds.top) ||
        !insertFence.BindInt(4, width) ||
        !insertFence.BindInt(5, height) ||
        !insertFence.BindText(6, fence.styleJson.empty() ? std::wstring(L"{}") : fence.styleJson) ||
        !insertFence.BindText(7, now) ||
        !insertFence.BindText(8, now) ||
        insertFence.Step() != SQLITE_DONE) {
        return 0;
    }

    return sqlite3_last_insert_rowid(database_->NativeHandle());
}

bool FenceRepository::UpdateFence(const FenceRecord& fence) {
    if (database_ == nullptr || !database_->IsOpen() || fence.id <= 0) {
        return false;
    }

    Statement updateFence;
    if (!database_->Prepare(
            "UPDATE Fences "
            "SET name=?1, left_px=?2, top_px=?3, width_px=?4, height_px=?5, style_json=?6, updated_at_utc=?7 "
            "WHERE id=?8;",
            &updateFence)) {
        return false;
    }

    const int width = fence.bounds.right - fence.bounds.left;
    const int height = fence.bounds.bottom - fence.bounds.top;
    return updateFence.BindText(1, fence.name.empty() ? std::wstring(L"Desktop Group") : fence.name) &&
           updateFence.BindInt(2, fence.bounds.left) &&
           updateFence.BindInt(3, fence.bounds.top) &&
           updateFence.BindInt(4, width) &&
           updateFence.BindInt(5, height) &&
           updateFence.BindText(6, fence.styleJson.empty() ? std::wstring(L"{}") : fence.styleJson) &&
           updateFence.BindText(7, UtcNowIso8601()) &&
           updateFence.BindInt64(8, fence.id) &&
           updateFence.Step() == SQLITE_DONE &&
           sqlite3_changes(database_->NativeHandle()) > 0;
}

bool FenceRepository::DeleteFence(long long fenceId) {
    if (database_ == nullptr || !database_->IsOpen() || fenceId <= 0) {
        return false;
    }

    Statement deleteFence;
    return database_->Prepare("DELETE FROM Fences WHERE id=?1;", &deleteFence) &&
           deleteFence.BindInt64(1, fenceId) &&
           deleteFence.Step() == SQLITE_DONE &&
           sqlite3_changes(database_->NativeHandle()) > 0;
}

bool FenceRepository::ReplaceFenceIcons(long long fenceId, const std::vector<FenceIconRecord>& icons) {
    if (database_ == nullptr || !database_->IsOpen() || fenceId <= 0) {
        return false;
    }

    if (!database_->BeginTransaction()) {
        return false;
    }

    Statement deleteExisting;
    if (!database_->Prepare("DELETE FROM FenceIcons WHERE fence_id=?1;", &deleteExisting) ||
        !deleteExisting.BindInt64(1, fenceId) ||
        deleteExisting.Step() != SQLITE_DONE) {
        const bool ignored = database_->RollbackTransaction();
        (void)ignored;
        return false;
    }

    Statement insertIcon;
    if (!database_->Prepare(
            "INSERT INTO FenceIcons("
            "fence_id, icon_identity, icon_name, order_index, current_x, current_y, original_x, original_y, updated_at_utc"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);",
            &insertIcon)) {
        const bool ignored = database_->RollbackTransaction();
        (void)ignored;
        return false;
    }

    for (size_t i = 0; i < icons.size(); ++i) {
        const FenceIconRecord& icon = icons[i];
        insertIcon.Reset();
        insertIcon.ClearBindings();
        if (!insertIcon.BindInt64(1, fenceId) ||
            !insertIcon.BindText(2, icon.iconIdentity) ||
            !insertIcon.BindText(3, icon.iconName) ||
            !insertIcon.BindInt(4, static_cast<int>(i)) ||
            !insertIcon.BindInt(5, icon.currentX) ||
            !insertIcon.BindInt(6, icon.currentY) ||
            !insertIcon.BindInt(7, icon.originalX) ||
            !insertIcon.BindInt(8, icon.originalY) ||
            !insertIcon.BindText(9, UtcNowIso8601()) ||
            insertIcon.Step() != SQLITE_DONE) {
            const bool ignored = database_->RollbackTransaction();
            (void)ignored;
            return false;
        }
    }

    if (!database_->CommitTransaction()) {
        const bool ignored = database_->RollbackTransaction();
        (void)ignored;
        return false;
    }
    return true;
}

std::vector<FenceRecord> FenceRepository::ListFences() {
    std::vector<FenceRecord> fences;
    if (database_ == nullptr || !database_->IsOpen()) {
        return fences;
    }

    Statement query;
    if (!database_->Prepare(
            "SELECT id, name, left_px, top_px, width_px, height_px, style_json, created_at_utc, updated_at_utc "
            "FROM Fences ORDER BY id ASC;",
            &query)) {
        return fences;
    }

    while (true) {
        const int step = query.Step();
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            break;
        }

        FenceRecord fence{};
        fence.id = query.ColumnInt64(0);
        fence.name = query.ColumnText(1);
        const int left = query.ColumnInt(2);
        const int top = query.ColumnInt(3);
        const int width = query.ColumnInt(4);
        const int height = query.ColumnInt(5);
        fence.bounds = RECT{left, top, left + width, top + height};
        fence.styleJson = query.ColumnText(6);
        fence.createdAtUtc = query.ColumnText(7);
        fence.updatedAtUtc = query.ColumnText(8);
        fences.push_back(std::move(fence));
    }
    return fences;
}

std::vector<FenceIconRecord> FenceRepository::ListFenceIcons(long long fenceId) {
    std::vector<FenceIconRecord> icons;
    if (database_ == nullptr || !database_->IsOpen() || fenceId <= 0) {
        return icons;
    }

    Statement query;
    if (!database_->Prepare(
            "SELECT id, fence_id, icon_identity, icon_name, order_index, current_x, current_y, original_x, original_y, updated_at_utc "
            "FROM FenceIcons WHERE fence_id=?1 ORDER BY order_index ASC, id ASC;",
            &query) ||
        !query.BindInt64(1, fenceId)) {
        return icons;
    }

    while (true) {
        const int step = query.Step();
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            break;
        }

        FenceIconRecord icon{};
        icon.id = query.ColumnInt64(0);
        icon.fenceId = query.ColumnInt64(1);
        icon.iconIdentity = query.ColumnText(2);
        icon.iconName = query.ColumnText(3);
        icon.orderIndex = query.ColumnInt(4);
        icon.currentX = query.ColumnInt(5);
        icon.currentY = query.ColumnInt(6);
        icon.originalX = query.ColumnInt(7);
        icon.originalY = query.ColumnInt(8);
        icon.updatedAtUtc = query.ColumnText(9);
        icons.push_back(std::move(icon));
    }
    return icons;
}
}  // namespace Persistence

