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

struct RestoreSessionRecord {
    long long id = 1;
    std::wstring lastExitMode;
    bool lastShutdownClean = true;
    bool lastRestoreNeeded = false;
    std::wstring updatedAtUtc;
};

struct FenceRecord {
    long long id = 0;
    std::wstring name;
    RECT bounds{0, 0, 0, 0};
    std::wstring styleJson;
    std::wstring createdAtUtc;
    std::wstring updatedAtUtc;
};

struct FenceIconRecord {
    long long id = 0;
    long long fenceId = 0;
    std::wstring iconIdentity;
    std::wstring iconName;
    int orderIndex = 0;
    int currentX = 0;
    int currentY = 0;
    int originalX = 0;
    int originalY = 0;
    std::wstring updatedAtUtc;
};

[[nodiscard]] std::wstring BuildIconIdentity(const Desktop::DesktopIcon& icon);
[[nodiscard]] std::wstring UtcNowIso8601();
}

