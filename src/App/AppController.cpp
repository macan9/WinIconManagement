#include "App/AppController.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "Infrastructure/Logger.h"
#include "Infrastructure/Paths.h"
#include "Persistence/Schema.h"
#include "Resource.h"

namespace {
constexpr wchar_t kMainWindowClassName[] = L"WinIconManagement.MainWindow";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kDesktopHealthTimerId = 1;
constexpr UINT kDesktopHealthIntervalMs = 3000;
constexpr int kGridPaddingPixels = 16;
constexpr int kMinimumGridSpacing = 48;

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

std::wstring PointToString(const POINT& point) {
    std::wstringstream stream;
    stream << L"(" << point.x << L"," << point.y << L")";
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

int ClampInt(int value, int minValue, int maxValue) {
    if (minValue > maxValue) {
        return minValue;
    }
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

int CeilDivide(int numerator, int denominator) {
    if (denominator <= 0) {
        return 0;
    }
    return (numerator + denominator - 1) / denominator;
}

int DetectAxisSpacing(
    const std::vector<Desktop::DesktopIcon>& icons,
    bool useXAxis,
    int fallback) {
    if (icons.size() < 2) {
        return fallback;
    }

    std::vector<int> axisValues;
    axisValues.reserve(icons.size());
    for (const Desktop::DesktopIcon& icon : icons) {
        axisValues.push_back(useXAxis ? icon.position.x : icon.position.y);
    }

    std::sort(axisValues.begin(), axisValues.end());
    axisValues.erase(std::unique(axisValues.begin(), axisValues.end()), axisValues.end());
    if (axisValues.size() < 2) {
        return fallback;
    }

    int minPositiveDelta = std::numeric_limits<int>::max();
    for (size_t i = 1; i < axisValues.size(); ++i) {
        const int delta = axisValues[i] - axisValues[i - 1];
        if (delta > 0 && delta < minPositiveDelta) {
            minPositiveDelta = delta;
        }
    }

    if (minPositiveDelta == std::numeric_limits<int>::max()) {
        return fallback;
    }

    if (minPositiveDelta < (fallback / 2) || minPositiveDelta > (fallback * 2)) {
        return fallback;
    }
    return minPositiveDelta;
}

struct GridMovePlan {
    std::vector<Desktop::DesktopIcon> iconsToMove;
    RECT workArea{0, 0, 0, 0};
    POINT origin{0, 0};
    int spacingX = 0;
    int spacingY = 0;
    int columns = 0;
    int rowsPerColumn = 0;
    int capacity = 0;
    int skippedCount = 0;
};

bool BuildGridMovePlan(
    const std::vector<Desktop::DesktopIcon>& sourceIcons,
    const RECT& workArea,
    GridMovePlan* outPlan) {
    if (outPlan == nullptr || sourceIcons.empty()) {
        return false;
    }

    const int width = workArea.right - workArea.left;
    const int height = workArea.bottom - workArea.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    const int fallbackSpacingX = std::max(GetSystemMetrics(SM_CXICONSPACING), kMinimumGridSpacing);
    const int fallbackSpacingY = std::max(GetSystemMetrics(SM_CYICONSPACING), kMinimumGridSpacing);
    const int spacingX = std::max(DetectAxisSpacing(sourceIcons, true, fallbackSpacingX), kMinimumGridSpacing);
    const int spacingY = std::max(DetectAxisSpacing(sourceIcons, false, fallbackSpacingY), kMinimumGridSpacing);

    int minX = static_cast<int>(sourceIcons.front().position.x);
    int minY = static_cast<int>(sourceIcons.front().position.y);
    for (const Desktop::DesktopIcon& icon : sourceIcons) {
        minX = std::min(minX, static_cast<int>(icon.position.x));
        minY = std::min(minY, static_cast<int>(icon.position.y));
    }

    const int leftBound = static_cast<int>(workArea.left) + kGridPaddingPixels;
    const int topBound = static_cast<int>(workArea.top) + kGridPaddingPixels;
    const int rightBound = std::max(leftBound, static_cast<int>(workArea.right) - kGridPaddingPixels);
    const int bottomBound = std::max(topBound, static_cast<int>(workArea.bottom) - kGridPaddingPixels);
    int maxX = minX;
    int maxY = minY;
    for (const Desktop::DesktopIcon& icon : sourceIcons) {
        maxX = std::max(maxX, static_cast<int>(icon.position.x));
        maxY = std::max(maxY, static_cast<int>(icon.position.y));
    }

    const int usableWidth = std::max(1, rightBound - leftBound + 1);
    const int usableHeight = std::max(1, bottomBound - topBound + 1);
    const int maxColumnsByWidth = std::max(1, usableWidth / spacingX);
    const int maxRowsByHeight = std::max(1, usableHeight / spacingY);
    const int iconCount = static_cast<int>(sourceIcons.size());
    const long long maxCellsWide = static_cast<long long>(maxColumnsByWidth);
    const long long maxCellsHigh = static_cast<long long>(maxRowsByHeight);
    const int capacity = static_cast<int>(std::max<long long>(1, maxCellsWide * maxCellsHigh));
    const int plannedCount = std::min(iconCount, capacity);

    int rowsPerColumn = std::max(1, CeilDivide(plannedCount, maxColumnsByWidth));
    rowsPerColumn = std::min(rowsPerColumn, maxRowsByHeight);
    rowsPerColumn = std::max(rowsPerColumn, 1);
    int columns = std::max(1, CeilDivide(plannedCount, rowsPerColumn));
    columns = std::min(columns, maxColumnsByWidth);

    const int layoutWidth = std::max(0, (columns - 1) * spacingX);
    const int layoutHeight = std::max(0, (rowsPerColumn - 1) * spacingY);
    const int startXMin = leftBound;
    const int startXMax = std::max(leftBound, rightBound - layoutWidth);
    const int startYMin = topBound;
    const int startYMax = std::max(topBound, bottomBound - layoutHeight);

    const int centerX = (minX + maxX) / 2;
    const int centerY = (minY + maxY) / 2;
    const int startX = (std::abs(centerX - startXMin) >= std::abs(centerX - startXMax)) ? startXMin : startXMax;
    const int startY = (std::abs(centerY - startYMin) >= std::abs(centerY - startYMax)) ? startYMin : startYMax;

    std::vector<Desktop::DesktopIcon> sortedIcons = sourceIcons;
    std::sort(
        sortedIcons.begin(),
        sortedIcons.end(),
        [](const Desktop::DesktopIcon& left, const Desktop::DesktopIcon& right) {
            if (left.position.x != right.position.x) {
                return left.position.x < right.position.x;
            }
            if (left.position.y != right.position.y) {
                return left.position.y < right.position.y;
            }
            return left.index < right.index;
        });

    std::vector<Desktop::DesktopIcon> targets;
    targets.reserve(static_cast<size_t>(plannedCount));
    for (int i = 0; i < plannedCount; ++i) {
        const int column = i / rowsPerColumn;
        const int row = i % rowsPerColumn;

        Desktop::DesktopIcon movedIcon = sortedIcons[static_cast<size_t>(i)];
        movedIcon.position.x = ClampInt(startX + column * spacingX, leftBound, rightBound);
        movedIcon.position.y = ClampInt(startY + row * spacingY, topBound, bottomBound);
        targets.push_back(std::move(movedIcon));
    }

    outPlan->iconsToMove = std::move(targets);
    outPlan->workArea = workArea;
    outPlan->origin = POINT{startX, startY};
    outPlan->spacingX = spacingX;
    outPlan->spacingY = spacingY;
    outPlan->columns = columns;
    outPlan->rowsPerColumn = rowsPerColumn;
    outPlan->capacity = capacity;
    outPlan->skippedCount = iconCount - plannedCount;
    return true;
}
}

namespace App {
AppController::AppController(HINSTANCE instance)
    : instance_(instance),
      mainWindow_(nullptr),
      diagnosticsTextControl_(nullptr),
      trayIcon_(),
      desktopIconCount_(0),
      desktopIconReadStatus_(L"Not started."),
      lastGridMoveSummary_(L"Not executed."),
      database_(),
      settingsRepository_(&database_),
      snapshotRepository_(&database_),
      persistenceReady_(false),
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
    if (!overlayWindow_.Initialize(instance_, mainWindow_)) {
        Infrastructure::Logger::Get().Error(L"[Overlay] initialization failed.");
    } else {
        UpdateOverlayWindow();
        overlayWindow_.Show();
    }
    if (!InitializePersistence()) {
        Infrastructure::Logger::Get().Error(L"Persistence initialization failed.");
    }
    ResolveDesktopWindows(false);
    if (SetTimer(mainWindow_, desktopHealthTimerId_, desktopHealthIntervalMs_, nullptr) == 0) {
        Infrastructure::Logger::Get().Error(L"SetTimer for desktop health monitor failed.");
    }
    UpdateWindowTitle();
    PersistBasicSettings();
    return true;
}

bool AppController::InitializePersistence() {
    const std::filesystem::path databasePath =
        Infrastructure::GetUserWritableAppDirectory() / L"data" / L"win_icon_management.db";

    if (!database_.Open(databasePath)) {
        desktopIconReadStatus_ = L"持久化初始化失败: 数据库打开失败";
        return false;
    }

    if (!Persistence::EnsureSchema(database_)) {
        desktopIconReadStatus_ = L"持久化初始化失败: 数据库结构迁移失败";
        database_.Close();
        return false;
    }

    persistenceReady_ = true;
    Infrastructure::Logger::Get().Info(
        L"[Persistence] ready. dbPath=" + databasePath.wstring() +
        L"; schemaVersion=" + std::to_wstring(Persistence::kDatabaseSchemaVersion));
    return true;
}

void AppController::PersistBasicSettings() {
    if (!persistenceReady_) {
        return;
    }
    const bool pinnedSaved = settingsRepository_.Upsert(L"is_pinned", isPinned_ ? L"1" : L"0");
    const bool pausedSaved = settingsRepository_.Upsert(L"is_paused", isPaused_ ? L"1" : L"0");
    if (!pinnedSaved || !pausedSaved) {
        Infrastructure::Logger::Get().Error(L"[Persistence] save basic settings failed.");
    }
}

void AppController::PersistIconSnapshot(const std::wstring& name, const std::wstring& source) {
    if (!persistenceReady_) {
        return;
    }
    if (desktopIcons_.empty()) {
        return;
    }

    const long long snapshotId = snapshotRepository_.SaveSnapshot(name, source, desktopIcons_);
    if (snapshotId <= 0) {
        Infrastructure::Logger::Get().Error(
            L"[Persistence] save snapshot failed. name=" + name + L"; source=" + source);
        return;
    }
    Infrastructure::Logger::Get().Info(
        L"[Persistence] snapshot saved. id=" + std::to_wstring(snapshotId) +
        L"; name=" + name +
        L"; source=" + source +
        L"; iconCount=" + std::to_wstring(desktopIcons_.size()));
}

bool AppController::EnsureDesktopConnection() {
    if (isDesktopConnected_ &&
        Desktop::DesktopWindowResolver::IsWindowChainValid(desktopResolveResult_)) {
        return true;
    }

    ResolveDesktopWindows(false);
    return isDesktopConnected_ &&
           Desktop::DesktopWindowResolver::IsWindowChainValid(desktopResolveResult_);
}

bool AppController::EnsureDesktopAndIconsReady() {
    if (!EnsureDesktopConnection()) {
        Infrastructure::Logger::Get().Error(L"[DesktopIcons] desktop connection unavailable.");
        return false;
    }

    if (desktopIcons_.empty()) {
        RefreshDesktopIconSnapshot();
    }
    if (desktopIcons_.empty()) {
        Infrastructure::Logger::Get().Error(L"[DesktopIcons] icon snapshot is empty.");
        return false;
    }
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

void AppController::UpdateOverlayWindow() {
    if (!overlayWindow_.IsInitialized()) {
        return;
    }

    const DisplayDiagnostics display = CollectDisplayDiagnostics();
    overlayWindow_.SetVirtualDesktopRect(display.virtualDesktopRect);

    RECT fenceRect{};
    const int width = display.virtualDesktopRect.right - display.virtualDesktopRect.left;
    const int height = display.virtualDesktopRect.bottom - display.virtualDesktopRect.top;
    const int fenceWidth = std::max(280, width / 4);
    const int fenceHeight = std::max(180, height / 4);
    fenceRect.left = display.virtualDesktopRect.left + std::max(48, width / 8);
    fenceRect.top = display.virtualDesktopRect.top + std::max(48, height / 8);
    fenceRect.right = fenceRect.left + fenceWidth;
    fenceRect.bottom = fenceRect.top + fenceHeight;

    overlayWindow_.SetFenceRect(fenceRect);
    overlayWindow_.SetFixedMode(!isPaused_);
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
        UpdateOverlayWindow();
        overlayWindow_.Show();
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
            UpdateOverlayWindow();
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
            UpdateOverlayWindow();
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
                overlayWindow_.Hide();
                ShowWindow(hwnd, SW_HIDE);
                Infrastructure::Logger::Get().Info(L"Main window hidden to tray.");
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, desktopHealthTimerId_);
            overlayWindow_.Destroy();
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
            if (overlayWindow_.IsInitialized()) {
                overlayWindow_.Show();
            }
            break;
        case IDM_TRAY_TOGGLE_PIN:
            isPinned_ = !isPinned_;
            trayIcon_.SetPinned(isPinned_);
            Infrastructure::Logger::Get().Info(
                isPinned_ ? L"Tray command: Pin enabled." : L"Tray command: Pin disabled.");
            UpdateWindowTitle();
            PersistBasicSettings();
            break;
        case IDM_TRAY_PAUSE:
            isPaused_ = !isPaused_;
            trayIcon_.SetPaused(isPaused_);
            Infrastructure::Logger::Get().Info(
                isPaused_ ? L"Tray command: Pause enabled." : L"Tray command: Pause disabled.");
            if (overlayWindow_.IsInitialized()) {
                overlayWindow_.SetFixedMode(!isPaused_);
                overlayWindow_.Show();
            }
            UpdateWindowTitle();
            PersistBasicSettings();
            break;
        case IDM_TRAY_RECONNECT_DESKTOP:
            Infrastructure::Logger::Get().Info(L"Tray command: Reconnect desktop.");
            ResolveDesktopWindows(true);
            UpdateOverlayWindow();
            UpdateWindowTitle();
            break;
        case IDM_TRAY_TEST_MOVE_ICON:
            Infrastructure::Logger::Get().Info(L"Tray command: Batch grid move icons.");
            if (!MoveTestDesktopIcon()) {
                MessageBoxW(
                    hwnd,
                    L"测试移动失败，请查看日志。",
                    L"WinIconManagement",
                    MB_OK | MB_ICONWARNING);
            }
            break;
        case IDM_TRAY_RESTORE_LAYOUT:
            Infrastructure::Logger::Get().Info(L"Tray command: Restore layout.");
            if (!RestoreOriginalDesktopLayout()) {
                MessageBoxW(
                    hwnd,
                    L"恢复原始布局失败，请先执行一次“批量网格移动”并查看日志。",
                    L"WinIconManagement",
                    MB_OK | MB_ICONWARNING);
            }
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
        RefreshDesktopIconSnapshot();
        if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
            InvalidateRect(mainWindow_, nullptr, TRUE);
        }
        UpdateDiagnosticsTextControl();
        return;
    }

    desktopIconCount_ = 0;
    desktopIcons_.clear();
    desktopIconReadStatus_ = L"Desktop not connected.";

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

void AppController::RefreshDesktopIconSnapshot() {
    desktopIconCount_ = desktopIconService_.GetDesktopIconCount(desktopResolveResult_.listViewWindow);
    desktopIcons_ = desktopIconService_.EnumerateDesktopIcons(
        desktopResolveResult_.listViewWindow,
        desktopResolveResult_.explorerProcessId);

    if (desktopIconCount_ <= 0) {
        desktopIconReadStatus_ = L"No desktop icons found.";
    } else if (desktopIcons_.empty()) {
        desktopIconReadStatus_ = L"Read failed: no icon details returned.";
    } else if (desktopIcons_.size() == static_cast<size_t>(desktopIconCount_)) {
        desktopIconReadStatus_ = L"Read completed.";
    } else {
        desktopIconReadStatus_ = L"Partial read: " + std::to_wstring(desktopIcons_.size()) +
                                 L"/" + std::to_wstring(desktopIconCount_);
    }

    LogDesktopIconDiagnostics();
}

void AppController::CacheOriginalIconPositions() {
    if (!desktopIcons_.empty()) {
        originalDesktopIcons_ = desktopIcons_;
        Infrastructure::Logger::Get().Info(
            L"[DesktopMove] original icon snapshot cached. count=" + std::to_wstring(originalDesktopIcons_.size()));
        PersistIconSnapshot(L"before_move", L"auto");
    }
}

bool AppController::MoveTestDesktopIcon() {
    if (!EnsureDesktopAndIconsReady()) {
        desktopIconReadStatus_ = L"测试移动失败: 桌面图标不可用";
        lastGridMoveSummary_ = L"失败: 桌面图标不可用";
        UpdateDiagnosticsTextControl();
        return false;
    }

    CacheOriginalIconPositions();
    const DisplayDiagnostics display = CollectDisplayDiagnostics();
    GridMovePlan plan{};
    if (!BuildGridMovePlan(desktopIcons_, display.virtualDesktopRect, &plan) ||
        plan.iconsToMove.empty()) {
        desktopIconReadStatus_ = L"测试移动失败: 无法计算可见网格目标点";
        lastGridMoveSummary_ = L"失败: 无法计算可见网格目标点";
        Infrastructure::Logger::Get().Error(
            L"[DesktopMove] bulk grid plan failed. iconCount=" + std::to_wstring(desktopIcons_.size()) +
            L"; workArea=" + RectToString(display.virtualDesktopRect));
        UpdateDiagnosticsTextControl();
        return false;
    }

    const int expected = static_cast<int>(plan.iconsToMove.size());
    int meaningfulMoveCount = 0;
    for (size_t i = 0; i < plan.iconsToMove.size(); ++i) {
        const Desktop::DesktopIcon& target = plan.iconsToMove[i];
        const Desktop::DesktopIcon* source = nullptr;
        for (const Desktop::DesktopIcon& candidate : desktopIcons_) {
            if (candidate.index == target.index) {
                source = &candidate;
                break;
            }
        }
        if (source == nullptr) {
            continue;
        }
        const int deltaX = std::abs(target.position.x - source->position.x);
        const int deltaY = std::abs(target.position.y - source->position.y);
        if (deltaX >= (plan.spacingX / 2) || deltaY >= (plan.spacingY / 2)) {
            ++meaningfulMoveCount;
        }
    }

    if (meaningfulMoveCount == 0) {
        desktopIconReadStatus_ = L"测试移动失败: 规划结果位移过小";
        lastGridMoveSummary_ = L"失败: 规划位移过小";
        Infrastructure::Logger::Get().Error(
            L"[DesktopMove] bulk grid plan rejected. meaningfulMoveCount=0; "
            L"origin=" + PointToString(plan.origin) +
            L"; spacing=(" + std::to_wstring(plan.spacingX) + L"," + std::to_wstring(plan.spacingY) + L")");
        UpdateDiagnosticsTextControl();
        return false;
    }

    const int movedCount = desktopIconService_.MoveDesktopIcons(
        desktopResolveResult_.listViewWindow,
        desktopResolveResult_.explorerProcessId,
        plan.iconsToMove);

    lastGridMoveSummary_ =
        L"origin=" + PointToString(plan.origin) +
        L", spacing=" + std::to_wstring(plan.spacingX) + L"x" + std::to_wstring(plan.spacingY) +
        L", grid=" + std::to_wstring(plan.columns) + L"x" + std::to_wstring(plan.rowsPerColumn) +
        L", planned=" + std::to_wstring(expected) +
        L", meaningful=" + std::to_wstring(meaningfulMoveCount) +
        L", moved=" + std::to_wstring(movedCount) +
        L", skipped=" + std::to_wstring(plan.skippedCount);

    Infrastructure::Logger::Get().Info(
        L"[DesktopMove] bulk grid result. moved=" + std::to_wstring(movedCount) +
        L"; expected=" + std::to_wstring(expected) +
        L"; meaningful=" + std::to_wstring(meaningfulMoveCount) +
        L"; skipped=" + std::to_wstring(plan.skippedCount) +
        L"; origin=" + PointToString(plan.origin) +
        L"; spacing=(" + std::to_wstring(plan.spacingX) + L"," + std::to_wstring(plan.spacingY) + L")" +
        L"; grid=(" + std::to_wstring(plan.columns) + L"," + std::to_wstring(plan.rowsPerColumn) + L")" +
        L"; capacity=" + std::to_wstring(plan.capacity) +
        L"; workArea=" + RectToString(plan.workArea));

    if (movedCount <= 0) {
        desktopIconReadStatus_ = L"测试移动失败: 批量移动未成功";
        lastGridMoveSummary_ = L"失败: " + lastGridMoveSummary_;
        UpdateDiagnosticsTextControl();
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[DesktopMove] bulk grid summary: " + lastGridMoveSummary_);
    RefreshDesktopIconSnapshot();
    PersistIconSnapshot(L"after_batch_move", L"auto");
    if (movedCount == expected) {
        desktopIconReadStatus_ = L"测试移动成功: 批量网格移动完成 " +
                                 std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
    } else {
        desktopIconReadStatus_ = L"测试移动部分成功: 批量网格移动 " +
                                 std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
    }
    UpdateDiagnosticsTextControl();
    return movedCount > 0;
}

bool AppController::RestoreOriginalDesktopLayout() {
    if (!EnsureDesktopConnection()) {
        desktopIconReadStatus_ = L"恢复失败: 桌面连接不可用";
        lastGridMoveSummary_ = L"恢复失败: 桌面连接不可用";
        UpdateDiagnosticsTextControl();
        return false;
    }
    if (originalDesktopIcons_.empty()) {
        desktopIconReadStatus_ = L"恢复失败: 没有可恢复的原始快照";
        lastGridMoveSummary_ = L"恢复失败: 没有可恢复的原始快照";
        Infrastructure::Logger::Get().Error(L"[DesktopMove] restore skipped: no original snapshot.");
        UpdateDiagnosticsTextControl();
        return false;
    }

    // Restore 前先做一次重连，避免 Explorer 在 move 后重启导致旧句柄失效。
    ResolveDesktopWindows(false);
    if (!isDesktopConnected_ ||
        !Desktop::DesktopWindowResolver::IsWindowChainValid(desktopResolveResult_)) {
        desktopIconReadStatus_ = L"恢复失败: 桌面句柄失效，重连未成功";
        lastGridMoveSummary_ = L"恢复失败: 桌面句柄失效";
        Infrastructure::Logger::Get().Error(L"[DesktopMove] restore aborted: desktop reconnect failed.");
        UpdateDiagnosticsTextControl();
        return false;
    }

    const int movedCount = desktopIconService_.MoveDesktopIcons(
        desktopResolveResult_.listViewWindow,
        desktopResolveResult_.explorerProcessId,
        originalDesktopIcons_);
    const int expected = static_cast<int>(originalDesktopIcons_.size());

    Infrastructure::Logger::Get().Info(
        L"[DesktopMove] restore result. moved=" + std::to_wstring(movedCount) +
        L"; expected=" + std::to_wstring(expected) +
        L"; explorerPid=" + std::to_wstring(desktopResolveResult_.explorerProcessId) +
        L"; listView=" + HandleToString(desktopResolveResult_.listViewWindow));

    RefreshDesktopIconSnapshot();
    PersistIconSnapshot(L"after_restore", L"auto");
    if (movedCount == expected) {
        desktopIconReadStatus_ = L"恢复完成";
        lastGridMoveSummary_ = L"恢复完成: " +
                               std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
    } else {
        desktopIconReadStatus_ = L"恢复部分成功: " +
                                 std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
        lastGridMoveSummary_ = L"恢复部分成功: " +
                               std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
    }
    UpdateDiagnosticsTextControl();
    return movedCount > 0;
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

void AppController::LogDesktopIconDiagnostics() const {
    std::wstring summary = L"[DesktopIcons] expectedCount=" + std::to_wstring(desktopIconCount_);
    summary += L"; readCount=" + std::to_wstring(desktopIcons_.size());
    summary += L"; status=" + desktopIconReadStatus_;
    Infrastructure::Logger::Get().Info(summary);

    const size_t sampleCount = std::min<size_t>(desktopIcons_.size(), 10);
    for (size_t i = 0; i < sampleCount; ++i) {
        const Desktop::DesktopIcon& icon = desktopIcons_[i];
        std::wstring line = L"[DesktopIcons] sample index=" + std::to_wstring(icon.index);
        line += L"; hasName=" + std::wstring(icon.displayName.empty() ? L"false" : L"true");
        line += L"; nameLength=" + std::to_wstring(icon.displayName.size());
        line += L"; position=" + PointToString(icon.position);
        Infrastructure::Logger::Get().Info(line);
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

    text += L"\r\n桌面图标预期数: " + std::to_wstring(desktopIconCount_) + L"\r\n";
    text += L"桌面图标读取数: " + std::to_wstring(desktopIcons_.size()) + L"\r\n";
    text += L"图标读取状态: " + desktopIconReadStatus_ + L"\r\n";
    text += L"批量移动摘要: " + lastGridMoveSummary_ + L"\r\n";
    text += L"持久化状态: " + std::wstring(persistenceReady_ ? L"ready" : L"not ready") + L"\r\n";
    if (persistenceReady_) {
        text += L"数据库路径: " + database_.DatabasePath().wstring() + L"\r\n";
    } else if (!database_.LastError().empty()) {
        text += L"数据库错误: " + database_.LastError() + L"\r\n";
    }
    text += L"原始快照缓存数: " + std::to_wstring(originalDesktopIcons_.size()) + L"\r\n";
    if (!desktopIcons_.empty()) {
        text += L"图标样例:\r\n";
        const size_t sampleCount = std::min<size_t>(desktopIcons_.size(), 10);
        for (size_t i = 0; i < sampleCount; ++i) {
            const Desktop::DesktopIcon& icon = desktopIcons_[i];
            text += L"  [" + std::to_wstring(icon.index) + L"] ";
            text += icon.displayName.empty() ? L"<empty>" : icon.displayName;
            text += L" @ " + PointToString(icon.position) + L"\r\n";
        }
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
