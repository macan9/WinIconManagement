#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Desktop/DesktopIconService.h"
#include "Persistence/Database.h"
#include "Persistence/Models.h"

namespace Persistence {
class SnapshotRepository {
public:
    explicit SnapshotRepository(Database* database);

    [[nodiscard]] long long SaveSnapshot(
        const std::wstring& name,
        const std::wstring& source,
        const std::vector<Desktop::DesktopIcon>& icons);

    [[nodiscard]] std::optional<SnapshotRecord> LoadLatestSnapshot();

private:
    Database* database_;
};
}  // namespace Persistence

