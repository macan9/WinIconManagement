#pragma once

#include "Persistence/Database.h"

namespace Persistence {
constexpr int kDatabaseSchemaVersion = 2;

[[nodiscard]] bool EnsureSchema(Database& database);
}

