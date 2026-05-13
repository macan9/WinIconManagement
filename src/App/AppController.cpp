#include "App/AppController.h"

#include <cstdint>
#include <sstream>
#include <string>

#include "Infrastructure/Logger.h"
#include "Resource.h"

namespace {
constexpr wchar_t kMainWindowClassName[] = L"WinIconManagement.MainWindow";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kDesktopHealthTimerId = 1;
constexpr UINT kDesktopHealthIntervalMs = 3000;

std::wstring HandleToString(HWND handle) {
    if (handle == nullptr) {
        return L"0x0";
    }
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(handle);
    return stream.str();
}

std::wstring RectToString(const RECT& rect) {
    std::wstringstream stream;
    stream << L"["
           << rect.left << L"," << rect.top << L"]-["
           << rect.right << L"," << rect.bottom << L"]";
    return stream.str();
}

struct DisplayDiagnostics {
    int monitorCount = 0;
    RECT virtualDesktopRect{0, 0, 0, 0};
    std::wstring monitorDetails;
};

struct MonitorEnumContext {
    int index = 0;
    std::wstring lines;
};

BOOL CALLBACK EnumDisplayMonitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
    auto* context = reinterpret_cast<MonitorEnumContext*>(parameter);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }

    ++context->index;
    context->lines += L"  ";
    context->lines += std::to_wstring(context->index);
    context->lines += L". ";
    context->lines += info.szDevice;
    context->lines += L" bounds=";
    context->lines += RectToString(info.rcMonitor);
    context->lines += L", work=";
    context->lines += RectToString(info.rcWork);
    context->lines += L", primary=";
    context->lines += (info.dwFlags & MONITORINFOF_PRIMARY) ? L"true" : L"false";
    context->lines += L"\r\n";
    return TRUE;
}

DisplayDiagnostics CollectDisplayDiagnostics() {
    DisplayDiagnostics diagnostics{};
    diagnostics.monitorCount = GetSystemMetrics(SM_CMONITORS);
    diagnostics.virtualDesktopRect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    diagnostics.virtualDesktopRect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    diagnostics.virtualDesktopRect.right =
        diagnostics.virtualDesktopRect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    diagnostics.virtualDesktopRect.bottom =
        diagnostics.virtualDesktopRect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    MonitorEnumContext context{};
    EnumDisplayMonitors(nullptr, nullptr, EnumDisplayMonitorCallback, reinterpret_cast<LPARAM>(&context));
    diagnostics.monitorDetails = context.lines;
    return diagnostics;
}
}

namespace App {
AppController::AppController(HINSTANCE instance)
    : instance_(instance),
      mainWindow_(nullptr),
      diagnosticsTextControl_(nullptr),
      trayIcon_(),
      trayCallbackMessage_(kTrayCallbackMessage),
      taskbarCreatedMessage_(RegisterWindowMessageW(L"TaskbarCreated")),
      desktopHealthTimerId_(kDesktopHealthTimerId),
      desktopHealthIntervalMs_(kDesktopHealthIntervalMs),
      isPinned_(false),
      isPaused_(false),
      isExiting_(false),
      isDesktopConnected_(false) {}

AppController::~AppController() {
    if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
        KillTimer(mainWindow_, desktopHealthTimerId_);
    }
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
    ResolveDesktopWindows(false);
    if (SetTimer(mainWindow_, desktopHealthTimerId_, desktopHealthIntervalMs_, nullptr) == 0) {
        Infrastructure::Logger::Get().Error(L"SetTimer for desktop health monitor failed.");
    }
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

    if (!CreateDiagnosticsTextControl()) {
        return false;
    }

    RECT clientRect{};
    GetClientRect(mainWindow_, &clientRect);
    LayoutDiagnosticsTextControl(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
    UpdateDiagnosticsTextControl();
    return true;
}

bool AppController::CreateDiagnosticsTextControl() {
    diagnosticsTextControl_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
        24,
        106,
        800,
        460,
        mainWindow_,
        nullptr,
        instance_,
        nullptr);

    if (diagnosticsTextControl_ == nullptr) {
        const DWORD error = GetLastError();
        Infrastructure::Logger::Get().Error(
            L"CreateWindowExW(EDIT) failed. error=" + std::to_wstring(error));
        return false;
    }

    SendMessageW(
        diagnosticsTextControl_,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
        TRUE);
    return true;
}

void AppController::LayoutDiagnosticsTextControl(int clientWidth, int clientHeight) {
    if (diagnosticsTextControl_ == nullptr || !IsWindow(diagnosticsTextControl_)) {
        return;
    }

    const int left = 24;
    const int top = 106;
    const int rightPadding = 24;
    const int bottomPadding = 24;
    const int minWidth = 120;
    const int minHeight = 80;

    int width = clientWidth - left - rightPadding;
    int height = clientHeight - top - bottomPadding;
    if (width < minWidth) {
        width = minWidth;
    }
    if (height < minHeight) {
        height = minHeight;
    }

    MoveWindow(diagnosticsTextControl_, left, top, width, height, TRUE);
}

void AppController::UpdateDiagnosticsTextControl() {
    if (diagnosticsTextControl_ == nullptr || !IsWindow(diagnosticsTextControl_)) {
        return;
    }

    const std::wstring text = BuildDesktopResolveStatusText();
    SetWindowTextW(diagnosticsTextControl_, text.c_str());
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

void AppController::ShowMainWindow() {
    if (mainWindow_ == nullptr || !IsWindow(mainWindow_)) {
        return;
    }

    if (!IsWindowVisible(mainWindow_)) {
        ShowWindow(mainWindow_, SW_SHOW);
    }

    if (IsIconic(mainWindow_)) {
        ShowWindow(mainWindow_, SW_RESTORE);
    } else {
        ShowWindow(mainWindow_, SW_SHOW);
    }

    SetForegroundWindow(mainWindow_);
    BringWindowToTop(mainWindow_);
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
        ResolveDesktopWindows(false);
        UpdateWindowTitle();
        return 0;
    }

    switch (message) {
        case WM_COMMAND:
            HandleCommand(hwnd, LOWORD(wParam));
            return 0;
        case WM_SIZE:
            LayoutDiagnosticsTextControl(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_TIMER:
            if (wParam == desktopHealthTimerId_) {
                if (!Desktop::DesktopWindowResolver::IsWindowChainValid(desktopResolveResult_)) {
                    Infrastructure::Logger::Get().Info(L"Desktop window handle invalid, reconnecting.");
                    ResolveDesktopWindows(false);
                    UpdateWindowTitle();
                }
                return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        case WM_DISPLAYCHANGE:
            Infrastructure::Logger::Get().Info(L"Display topology changed, refreshing desktop resolve.");
            ResolveDesktopWindows(false);
            UpdateWindowTitle();
            return 0;
        case WM_DPICHANGED: {
            const auto* suggestedRect = reinterpret_cast<RECT*>(lParam);
            if (suggestedRect != nullptr) {
                SetWindowPos(
                    hwnd,
                    nullptr,
                    suggestedRect->left,
                    suggestedRect->top,
                    suggestedRect->right - suggestedRect->left,
                    suggestedRect->bottom - suggestedRect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            Infrastructure::Logger::Get().Info(L"DPI changed, refreshing desktop resolve.");
            ResolveDesktopWindows(false);
            UpdateWindowTitle();
            return 0;
        }
        case WM_PAINT:
            PaintMainWindow(hwnd);
            return 0;
        case kTrayCallbackMessage:
            if (trayIcon_.HandleCallbackMessage(lParam)) {
                return 0;
            }
            return 0;
        case WM_CLOSE:
            if (!isExiting_) {
                ShowWindow(hwnd, SW_HIDE);
                Infrastructure::Logger::Get().Info(L"Main window hidden to tray.");
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, desktopHealthTimerId_);
            trayIcon_.Remove();
            if (isExiting_) {
                Infrastructure::Logger::Get().Info(L"Application is shutting down from tray command.");
            }
            mainWindow_ = nullptr;
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
            ShowMainWindow();
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
        case IDM_TRAY_RECONNECT_DESKTOP:
            Infrastructure::Logger::Get().Info(L"Tray command: Reconnect desktop.");
            ResolveDesktopWindows(true);
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

void AppController::PaintMainWindow(HWND hwnd) {
    PAINTSTRUCT paintStruct{};
    HDC deviceContext = BeginPaint(hwnd, &paintStruct);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    SetBkMode(deviceContext, TRANSPARENT);

    HGDIOBJ previousFont = SelectObject(deviceContext, GetStockObject(DEFAULT_GUI_FONT));

    RECT titleRect{24, 20, clientRect.right - 24, 48};
    DrawTextW(deviceContext, L"WinIconManagement", -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT labelRect{24, 48, clientRect.right - 24, 72};
    DrawTextW(deviceContext, L"桌面连接状态", -1, &labelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const COLORREF statusColor = isDesktopConnected_ ? RGB(40, 167, 69) : RGB(220, 53, 69);
    HBRUSH statusBrush = CreateSolidBrush(statusColor);
    HPEN statusPen = CreatePen(PS_SOLID, 1, statusColor);
    HGDIOBJ previousBrush = SelectObject(deviceContext, statusBrush);
    HGDIOBJ previousPen = SelectObject(deviceContext, statusPen);
    Ellipse(deviceContext, 24, 82, 36, 94);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);
    DeleteObject(statusPen);
    DeleteObject(statusBrush);

    const wchar_t* statusText = isDesktopConnected_ ? L"连接桌面成功" : L"桌面连接失败";
    RECT statusRect{44, 78, clientRect.right - 24, 98};
    DrawTextW(deviceContext, statusText, -1, &statusRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(deviceContext, previousFont);
    EndPaint(hwnd, &paintStruct);
}

void AppController::ResolveDesktopWindows(bool fromManualReconnect) {
    desktopResolveResult_ = desktopResolver_.Resolve();
    isDesktopConnected_ = desktopResolveResult_.success &&
                          Desktop::DesktopWindowResolver::IsWindowChainValid(desktopResolveResult_);

    LogDesktopResolveDiagnostics();

    if (isDesktopConnected_) {
        if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
            InvalidateRect(mainWindow_, nullptr, TRUE);
        }
        UpdateDiagnosticsTextControl();
        return;
    }

    std::wstring message = L"Desktop resolve failed at ";
    message += desktopResolveResult_.failureStep.empty() ? L"<unknown>" : desktopResolveResult_.failureStep;
    message += L", error=" + std::to_wstring(desktopResolveResult_.failureCode);
    Infrastructure::Logger::Get().Error(message);

    if (fromManualReconnect) {
        MessageBoxW(
            mainWindow_,
            L"重新连接桌面失败，请查看日志后重试。",
            L"WinIconManagement",
            MB_OK | MB_ICONWARNING);
    }

    if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
        InvalidateRect(mainWindow_, nullptr, TRUE);
    }
    UpdateDiagnosticsTextControl();
}

void AppController::LogDesktopResolveDiagnostics() const {
    std::wstring summary = L"[DesktopResolve] success=";
    summary += isDesktopConnected_ ? L"true" : L"false";
    summary += L"; path=" + desktopResolveResult_.resolvePath;
    summary += L"; fallback=" + std::wstring(desktopResolveResult_.usedEnumWindowsFallback ? L"true" : L"false");
    summary += L"; progman=" + HandleToString(desktopResolveResult_.progmanWindow);
    summary += L"; worker=" + HandleToString(desktopResolveResult_.workerWindow);
    summary += L"; defView=" + HandleToString(desktopResolveResult_.shellDefViewWindow);
    summary += L"; listView=" + HandleToString(desktopResolveResult_.listViewWindow);
    summary += L"; explorerPid=" + std::to_wstring(desktopResolveResult_.explorerProcessId);
    summary += L"; progmanClass=" + desktopResolveResult_.progmanClassName;
    summary += L"; workerClass=" + desktopResolveResult_.workerClassName;
    summary += L"; defViewClass=" + desktopResolveResult_.shellDefViewClassName;
    summary += L"; listViewClass=" + desktopResolveResult_.listViewClassName;
    if (!desktopResolveResult_.failureStep.empty()) {
        summary += L"; failureStep=" + desktopResolveResult_.failureStep;
        summary += L"; failureCode=" + std::to_wstring(desktopResolveResult_.failureCode);
    }

    if (isDesktopConnected_) {
        Infrastructure::Logger::Get().Info(summary);
    } else {
        Infrastructure::Logger::Get().Error(summary);
    }
}

std::wstring AppController::BuildDesktopResolveStatusText() const {
    std::wstring wrappedPath = desktopResolveResult_.resolvePath;
    const std::wstring pathDelimiter = L" -> ";
    const std::wstring wrappedDelimiter = L"\r\n  -> ";
    size_t delimiterPosition = 0;
    while ((delimiterPosition = wrappedPath.find(pathDelimiter, delimiterPosition)) != std::wstring::npos) {
        wrappedPath.replace(delimiterPosition, pathDelimiter.size(), wrappedDelimiter);
        delimiterPosition += wrappedDelimiter.size();
    }

    std::wstring text;
    text += L"解析路径:\r\n";
    text += L"  " + wrappedPath + L"\r\n";
    text += L"Fallback: " + std::wstring(desktopResolveResult_.usedEnumWindowsFallback ? L"true" : L"false") + L"\r\n";
    text += L"Progman: " + HandleToString(desktopResolveResult_.progmanWindow) +
            L" (" + desktopResolveResult_.progmanClassName + L")\r\n";
    text += L"WorkerW: " + HandleToString(desktopResolveResult_.workerWindow) +
            L" (" + desktopResolveResult_.workerClassName + L")\r\n";
    text += L"SHELLDLL_DefView: " + HandleToString(desktopResolveResult_.shellDefViewWindow) +
            L" (" + desktopResolveResult_.shellDefViewClassName + L")\r\n";
    text += L"SysListView32: " + HandleToString(desktopResolveResult_.listViewWindow) +
            L" (" + desktopResolveResult_.listViewClassName + L")\r\n";
    text += L"Explorer PID: " + std::to_wstring(desktopResolveResult_.explorerProcessId) + L"\r\n";
    const DisplayDiagnostics display = CollectDisplayDiagnostics();
    text += L"显示器数量: " + std::to_wstring(display.monitorCount) + L"\r\n";
    text += L"虚拟桌面范围: " + RectToString(display.virtualDesktopRect) + L"\r\n";
    if (!display.monitorDetails.empty()) {
        text += L"显示器详情:\r\n" + display.monitorDetails;
    }

    if (!isDesktopConnected_) {
        text += L"失败步骤: " +
                (desktopResolveResult_.failureStep.empty() ? std::wstring(L"<unknown>") : desktopResolveResult_.failureStep) +
                L"\r\n";
        text += L"失败码: " + std::to_wstring(desktopResolveResult_.failureCode) + L"\r\n";
    }

    return text;
}
}  // namespace App
