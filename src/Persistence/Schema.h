#pragma once

#include "Persistence/Database.h"

namespace Persistence {
constexpr int kDatabaseSchemaVersion = 1;

[[nodiscard]] bool EnsureSchema(Database& database);
}

