#pragma once

#include <windows.h>

#include "Desktop/DesktopWindowResolver.h"
#include "Tray/TrayIcon.h"

namespace App {
class AppController {
public:
    explicit AppController(HINSTANCE instance);
    ~AppController();

    bool Initialize();
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void HandleCommand(HWND hwnd, WORD commandId);
    bool RegisterWindowClass();
    bool CreateMainWindow();
    bool InitializeTray();
    void PaintMainWindow(HWND hwnd);
    void ResolveDesktopWindows(bool fromManualReconnect);
    void UpdateWindowTitle();

    HINSTANCE instance_;
    HWND mainWindow_;
    Tray::TrayIcon trayIcon_;
    Desktop::DesktopWindowResolver desktopResolver_;
    Desktop::DesktopResolveResult desktopResolveResult_;
    UINT trayCallbackMessage_;
    UINT taskbarCreatedMessage_;
    UINT_PTR desktopHealthTimerId_;
    UINT desktopHealthIntervalMs_;
    bool isPinned_;
    bool isPaused_;
    bool isExiting_;
    bool isDesktopConnected_;
};
}  // namespace App
