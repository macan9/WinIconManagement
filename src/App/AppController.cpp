#include "App/AppController.h"

#include <string>

#include "Infrastructure/Logger.h"
#include "Resource.h"

namespace {
constexpr wchar_t kMainWindowClassName[] = L"WinIconManagement.MainWindow";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
}

namespace App {
AppController::AppController(HINSTANCE instance)
    : instance_(instance),
      mainWindow_(nullptr),
      trayIcon_(),
      trayCallbackMessage_(kTrayCallbackMessage),
      taskbarCreatedMessage_(RegisterWindowMessageW(L"TaskbarCreated")),
      isPinned_(false),
      isPaused_(false),
      isExiting_(false),
      isDesktopConnected_(false) {}

AppController::~AppController() {
    trayIcon_.Remove();
    if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
        DestroyWindow(mainWindow_);
        mainWindow_ = nullptr;
    }
}

bool AppController::Initialize() {
    if (!RegisterWindowClass()) {
        return false;
    }
    if (!CreateMainWindow()) {
        return false;
    }
    if (!InitializeTray()) {
        return false;
    }
    isDesktopConnected_ = true;
    UpdateWindowTitle();
    return true;
}

int AppController::Run() {
    ShowWindow(mainWindow_, SW_SHOWDEFAULT);
    UpdateWindow(mainWindow_);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool AppController::RegisterWindowClass() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &AppController::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kMainWindowClassName;
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            Infrastructure::Logger::Get().Error(
                L"RegisterClassExW failed. error=" + std::to_wstring(error));
            return false;
        }
    }
    return true;
}

bool AppController::CreateMainWindow() {
    mainWindow_ = CreateWindowExW(
        0,
        kMainWindowClassName,
        L"WinIconManagement - Initialization Stage",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        600,
        nullptr,
        nullptr,
        instance_,
        this);

    if (mainWindow_ == nullptr) {
        const DWORD error = GetLastError();
        Infrastructure::Logger::Get().Error(
            L"CreateWindowExW failed. error=" + std::to_wstring(error));
        return false;
    }
    return true;
}

bool AppController::InitializeTray() {
    if (!trayIcon_.Initialize(instance_, mainWindow_, trayCallbackMessage_)) {
        Infrastructure::Logger::Get().Error(L"Tray icon initialization failed.");
        return false;
    }
    trayIcon_.SetPinned(isPinned_);
    trayIcon_.SetPaused(isPaused_);
    if (!trayIcon_.Show()) {
        Infrastructure::Logger::Get().Error(L"Failed to show tray icon.");
        return false;
    }
    return true;
}

LRESULT CALLBACK AppController::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AppController* controller = nullptr;

    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        controller = reinterpret_cast<AppController*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controller));
    } else {
        controller = reinterpret_cast<AppController*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (controller != nullptr) {
        return controller->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT AppController::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_) {
        trayIcon_.SetPinned(isPinned_);
        trayIcon_.SetPaused(isPaused_);
        trayIcon_.RecreateAfterExplorerRestart();
        return 0;
    }

    switch (message) {
        case WM_COMMAND:
            HandleCommand(hwnd, LOWORD(wParam));
            return 0;
        case kTrayCallbackMessage:
            if (trayIcon_.HandleCallbackMessage(lParam)) {
                return 0;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            trayIcon_.Remove();
            if (isExiting_) {
                Infrastructure::Logger::Get().Info(L"Application is shutting down from tray command.");
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void AppController::HandleCommand(HWND hwnd, WORD commandId) {
    switch (commandId) {
        case IDM_TRAY_SETTINGS:
            Infrastructure::Logger::Get().Info(L"Tray command: Settings.");
            MessageBoxW(hwnd, L"Settings window will be implemented in Stage 10.", L"WinIconManagement", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_TRAY_TOGGLE_PIN:
            isPinned_ = !isPinned_;
            trayIcon_.SetPinned(isPinned_);
            Infrastructure::Logger::Get().Info(
                isPinned_ ? L"Tray command: Pin enabled." : L"Tray command: Pin disabled.");
            UpdateWindowTitle();
            break;
        case IDM_TRAY_PAUSE:
            isPaused_ = !isPaused_;
            trayIcon_.SetPaused(isPaused_);
            Infrastructure::Logger::Get().Info(
                isPaused_ ? L"Tray command: Pause enabled." : L"Tray command: Pause disabled.");
            UpdateWindowTitle();
            break;
        case IDM_TRAY_RESTORE_LAYOUT:
            Infrastructure::Logger::Get().Info(L"Tray command: Restore layout.");
            MessageBoxW(hwnd, L"Restore layout will be implemented in Stage 09.", L"WinIconManagement", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_TRAY_EXIT:
            isExiting_ = true;
            Infrastructure::Logger::Get().Info(L"Tray command: Exit.");
            DestroyWindow(hwnd);
            break;
        default:
            break;
    }
}

void AppController::UpdateWindowTitle() {
    std::wstring title = L"WinIconManagement | ";
    title += isPinned_ ? L"Pinned" : L"Unpinned";
    title += L" | ";
    title += isPaused_ ? L"Paused" : L"Active";
    title += L" | ";
    title += isDesktopConnected_ ? L"DesktopConnected" : L"DesktopDisconnected";
    SetWindowTextW(mainWindow_, title.c_str());
}
}  // namespace App
