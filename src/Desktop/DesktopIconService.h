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
    [[nodiscard]] bool CanAccessExplorerProcess(DWORD explorerProcessId) const;
    [[nodiscard]] bool SetDesktopIconPosition(
        HWND listViewWindow,
        DWORD explorerProcessId,
        int iconIndex,
        POINT targetPosition) const;
    [[nodiscard]] int MoveDesktopIcons(
        HWND listViewWindow,
        DWORD explorerProcessId,
        const std::vector<DesktopIcon>& iconsToMove) const;
    [[nodiscard]] bool RefreshDesktopIcon(HWND listViewWindow, int iconIndex) const;
    [[nodiscard]] int HitTestDesktopIcon(
        HWND listViewWindow,
        DWORD explorerProcessId,
        POINT screenPoint) const;
};
}  // namespace Desktop
