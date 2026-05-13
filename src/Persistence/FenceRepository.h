#pragma once

#include "Persistence/Database.h"

namespace Persistence {
class FenceRepository {
public:
    explicit FenceRepository(Database* database);

private:
    Database* database_;
};
}  // namespace Persistence

