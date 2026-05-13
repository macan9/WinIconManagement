#include <windows.h>

#include "App/AppController.h"
#include "App/SingleInstance.h"
#include "Infrastructure/Dpi.h"
#include "Infrastructure/Logger.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Infrastructure::EnablePerMonitorDpiAwareness();

    if (!Infrastructure::Logger::Get().Initialize()) {
        MessageBoxW(nullptr, L"Failed to initialize logging.", L"WinIconManagement", MB_OK | MB_ICONERROR);
        return 1;
    }

    Infrastructure::Logger::Get().Info(L"Application bootstrap started.");

    App::SingleInstance singleInstance(L"WinIconManagement.Singleton");
    if (!singleInstance.Acquire()) {
        Infrastructure::Logger::Get().Info(L"Another instance is already running.");
        MessageBoxW(nullptr, L"WinIconManagement is already running.", L"WinIconManagement", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    App::AppController app(instance);
    if (!app.Initialize()) {
        Infrastructure::Logger::Get().Error(L"Failed to initialize application controller.");
        MessageBoxW(nullptr, L"Failed to initialize application.", L"WinIconManagement", MB_OK | MB_ICONERROR);
        return 1;
    }

    const int exitCode = app.Run();
    Infrastructure::Logger::Get().Info(L"Application exited cleanly.");
    return exitCode;
}

