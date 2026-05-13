#include "App/AppController.h"

#include <string>

#include "Infrastructure/Logger.h"

namespace {
constexpr wchar_t kMainWindowClassName[] = L"WinIconManagement.MainWindow";
}

namespace App {
AppController::AppController(HINSTANCE instance) : instance_(instance), mainWindow_(nullptr) {}

AppController::~AppController() {
    if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
        DestroyWindow(mainWindow_);
        mainWindow_ = nullptr;
    }
}

bool AppController::Initialize() {
    if (!RegisterWindowClass()) {
        return false;
    }
    return CreateMainWindow();
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
    switch (message) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}  // namespace App
