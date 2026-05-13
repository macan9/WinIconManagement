#include "Persistence/SnapshotRepository.h"

#include <winsqlite/winsqlite3.h>

namespace Persistence {
SnapshotRepository::SnapshotRepository(Database* database) : database_(database) {}

long long SnapshotRepository::SaveSnapshot(
    const std::wstring& name,
    const std::wstring& source,
    const std::vector<Desktop::DesktopIcon>& icons) {
    if (database_ == nullptr || !database_->IsOpen()) {
        return 0;
    }
    if (icons.empty()) {
        return 0;
    }

    if (!database_->BeginTransaction()) {
        return 0;
    }

    Statement insertSnapshot;
    if (!database_->Prepare(
            "INSERT INTO Snapshots(name, source, created_at_utc) VALUES(?1, ?2, ?3);",
            &insertSnapshot) ||
        !insertSnapshot.BindText(1, name) ||
        !insertSnapshot.BindText(2, source) ||
        !insertSnapshot.BindText(3, UtcNowIso8601()) ||
        insertSnapshot.Step() != SQLITE_DONE) {
        const bool rolledBack = database_->RollbackTransaction();
        (void)rolledBack;
        return 0;
    }

    const long long snapshotId = sqlite3_last_insert_rowid(database_->NativeHandle());
    if (snapshotId <= 0) {
        const bool rolledBack = database_->RollbackTransaction();
        (void)rolledBack;
        return 0;
    }

    Statement insertIcon;
    if (!database_->Prepare(
            "INSERT INTO SnapshotIcons(snapshot_id, icon_identity, icon_name, icon_index, x, y) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6);",
            &insertIcon)) {
        const bool rolledBack = database_->RollbackTransaction();
        (void)rolledBack;
        return 0;
    }

    for (const Desktop::DesktopIcon& icon : icons) {
        insertIcon.Reset();
        insertIcon.ClearBindings();
        if (!insertIcon.BindInt64(1, snapshotId) ||
            !insertIcon.BindText(2, BuildIconIdentity(icon)) ||
            !insertIcon.BindText(3, icon.displayName) ||
            !insertIcon.BindInt(4, icon.index) ||
            !insertIcon.BindInt(5, icon.position.x) ||
            !insertIcon.BindInt(6, icon.position.y) ||
            insertIcon.Step() != SQLITE_DONE) {
            const bool rolledBack = database_->RollbackTransaction();
            (void)rolledBack;
            return 0;
        }
    }

    if (!database_->CommitTransaction()) {
        const bool rolledBack = database_->RollbackTransaction();
        (void)rolledBack;
        return 0;
    }
    return snapshotId;
}

std::optional<SnapshotRecord> SnapshotRepository::LoadLatestSnapshot() {
    if (database_ == nullptr || !database_->IsOpen()) {
        return std::nullopt;
    }

    Statement querySnapshot;
    if (!database_->Prepare(
            "SELECT id, name, source, created_at_utc "
            "FROM Snapshots ORDER BY id DESC LIMIT 1;",
            &querySnapshot)) {
        return std::nullopt;
    }

    if (querySnapshot.Step() != SQLITE_ROW) {
        return std::nullopt;
    }

    SnapshotRecord snapshot{};
    snapshot.id = querySnapshot.ColumnInt64(0);
    snapshot.name = querySnapshot.ColumnText(1);
    snapshot.source = querySnapshot.ColumnText(2);
    snapshot.createdAtUtc = querySnapshot.ColumnText(3);

    Statement queryIcons;
    if (!database_->Prepare(
            "SELECT icon_index, icon_identity, icon_name, x, y "
            "FROM SnapshotIcons WHERE snapshot_id=?1 ORDER BY id ASC;",
            &queryIcons) ||
        !queryIcons.BindInt64(1, snapshot.id)) {
        return std::nullopt;
    }

    while (true) {
        const int stepResult = queryIcons.Step();
        if (stepResult == SQLITE_DONE) {
            break;
        }
        if (stepResult != SQLITE_ROW) {
            return std::nullopt;
        }

        SnapshotIconRecord item{};
        item.iconIndex = queryIcons.ColumnInt(0);
        item.iconIdentity = queryIcons.ColumnText(1);
        item.iconName = queryIcons.ColumnText(2);
        item.x = queryIcons.ColumnInt(3);
        item.y = queryIcons.ColumnInt(4);
        snapshot.icons.push_back(std::move(item));
    }

    return snapshot;
}
}  // namespace Persistence
