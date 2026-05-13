#pragma once

#include <string>
#include <vector>

#include "Persistence/Database.h"
#include "Persistence/Models.h"

namespace Persistence {
class SettingsRepository {
public:
    explicit SettingsRepository(Database* database);

    [[nodiscard]] bool Upsert(const std::wstring& key, const std::wstring& value);
    [[nodiscard]] bool TryGet(const std::wstring& key, std::wstring* outValue);
    [[nodiscard]] std::vector<AppSettingRecord> ListAll();

private:
    Database* database_;
};
}  // namespace Persistence

