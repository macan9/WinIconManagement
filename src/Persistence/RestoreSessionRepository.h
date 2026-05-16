#pragma once

#include <optional>

#include "Persistence/Database.h"
#include "Persistence/Models.h"

namespace Persistence {
class RestoreSessionRepository {
public:
    explicit RestoreSessionRepository(Database* database);

    [[nodiscard]] std::optional<RestoreSessionRecord> Load();
    [[nodiscard]] bool Save(const RestoreSessionRecord& record);

private:
    Database* database_;
};
}  // namespace Persistence
