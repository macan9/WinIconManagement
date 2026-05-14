#pragma once

#include <vector>

#include "Persistence/Database.h"
#include "Persistence/Models.h"

namespace Persistence {
class FenceRepository {
public:
    explicit FenceRepository(Database* database);

    [[nodiscard]] long long CreateFence(const FenceRecord& fence);
    [[nodiscard]] bool UpdateFence(const FenceRecord& fence);
    [[nodiscard]] bool DeleteFence(long long fenceId);
    [[nodiscard]] bool ReplaceFenceIcons(long long fenceId, const std::vector<FenceIconRecord>& icons);
    [[nodiscard]] std::vector<FenceRecord> ListFences();
    [[nodiscard]] std::vector<FenceIconRecord> ListFenceIcons(long long fenceId);

private:
    Database* database_;
};
}  // namespace Persistence

