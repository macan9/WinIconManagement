#pragma once

#include <windows.h>

#include <string>

namespace Tray {
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Initialize(HINSTANCE instance, HWND ownerWindow, UINT callbackMessage);
    bool Show();
    void Remove();
    bool HandleCallbackMessage(LPARAM lParam);
    bool RecreateAfterExplorerRestart();
    void SetPinned(bool pinned);
    void SetPaused(bool paused);

private:
    bool LoadResources();
    void ShowContextMenu();
    void UpdateMenuState();
    HICON LoadAppIcon() const;

    HINSTANCE instance_;
    HWND ownerWindow_;
    UINT callbackMessage_;
    UINT iconId_;
    bool isVisible_;
    bool isPinned_;
    bool isPaused_;
    HMENU trayMenu_;
    HICON trayIcon_;
    std::wstring tooltip_;
};
}  // namespace Tray

