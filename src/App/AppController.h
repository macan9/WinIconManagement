#pragma once

#include <windows.h>

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
    void UpdateWindowTitle();

    HINSTANCE instance_;
    HWND mainWindow_;
    Tray::TrayIcon trayIcon_;
    UINT trayCallbackMessage_;
    UINT taskbarCreatedMessage_;
    bool isPinned_;
    bool isPaused_;
    bool isExiting_;
    bool isDesktopConnected_;
};
}  // namespace App
