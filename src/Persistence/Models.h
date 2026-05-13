#pragma once

#include <string>
#include <vector>

#include "Desktop/DesktopIconService.h"

namespace Persistence {
struct SnapshotIconRecord {
    int iconIndex = -1;
    std::wstring iconIdentity;
    std::wstring iconName;
    int x = 0;
    int y = 0;
};

struct SnapshotRecord {
    long long id = 0;
    std::wstring name;
    std::wstring source;
    std::wstring createdAtUtc;
    std::vector<SnapshotIconRecord> icons;
};

struct AppSettingRecord {
    std::wstring key;
    std::wstring value;
    std::wstring updatedAtUtc;
};

[[nodiscard]] std::wstring BuildIconIdentity(const Desktop::DesktopIcon& icon);
[[nodiscard]] std::wstring UtcNowIso8601();
}

