#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace Desktop {
struct DesktopIcon {
    int index = -1;
    std::wstring displayName;
    POINT position{0, 0};
};

class DesktopIconService {
public:
    [[nodiscard]] int GetDesktopIconCount(HWND listViewWindow) const;
    [[nodiscard]] std::vector<DesktopIcon> EnumerateDesktopIcons(
        HWND listViewWindow,
        DWORD explorerProcessId) const;
};
}  // namespace Desktop
