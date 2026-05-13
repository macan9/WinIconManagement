#pragma once

#include <windows.h>

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
    bool RegisterWindowClass();
    bool CreateMainWindow();

    HINSTANCE instance_;
    HWND mainWindow_;
};
}  // namespace App
