#include "App/AppController.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
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
constexpr UINT kDefaultDpi = 96;
constexpr int kGridPaddingPixels = 16;
constexpr int kMinimumGridSpacing = 48;
constexpr int kBaseWindowWidth = 1280;
constexpr int kBaseWindowHeight = 900;
constexpr int kMinWindowWidth = 960;
constexpr int kMinWindowHeight = 680;
constexpr int kSelectionMinWidth = 40;
constexpr int kSelectionMinHeight = 40;
constexpr int kFenceInnerPadding = 16;

UINT GetDpiForWindowCompat(HWND window) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        const auto getDpiForWindow =
            reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpiForWindow != nullptr && window != nullptr) {
            const UINT dpi = getDpiForWindow(window);
            if (dpi != 0) {
                return dpi;
            }
        }
    }

    HDC screenDc = GetDC(window);
    const int dpi = screenDc != nullptr ? GetDeviceCaps(screenDc, LOGPIXELSX) : static_cast<int>(kDefaultDpi);
    if (screenDc != nullptr) {
        ReleaseDC(window, screenDc);
    }
    return dpi > 0 ? static_cast<UINT>(dpi) : kDefaultDpi;
}

bool AdjustWindowRectForDpiCompat(RECT* rect, DWORD style, BOOL hasMenu, DWORD exStyle, UINT dpi) {
    using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        const auto adjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(
            GetProcAddress(user32, "AdjustWindowRectExForDpi"));
        if (adjustWindowRectExForDpi != nullptr) {
            return adjustWindowRectExForDpi(rect, style, hasMenu, exStyle, dpi) != FALSE;
        }
    }

    return AdjustWindowRectEx(rect, style, hasMenu, exStyle) != FALSE;
}

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
    RECT primaryMonitorRect{0, 0, 0, 0};
    bool hasPrimaryMonitor = false;
    std::wstring monitorDetails;
};

struct MonitorEnumContext {
    int index = 0;
    RECT primaryMonitorRect{0, 0, 0, 0};
    bool hasPrimaryMonitor = false;
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

    if ((info.dwFlags & MONITORINFOF_PRIMARY) != 0) {
        context->primaryMonitorRect = info.rcMonitor;
        context->hasPrimaryMonitor = true;
    }
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
    diagnostics.primaryMonitorRect = context.primaryMonitorRect;
    diagnostics.hasPrimaryMonitor = context.hasPrimaryMonitor;
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

struct SelectionCompletePayload {
    RECT rect{};
    POINT releasePoint{};
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
      pendingSelectionRect_{0, 0, 0, 0},
      hasPendingSelectionRect_(false),
      desktopIconReadStatus_(L"Not started."),
      lastGridMoveSummary_(L"Not executed."),
      database_(),
      fenceRepository_(&database_),
      settingsRepository_(&database_),
      snapshotRepository_(&database_),
      persistenceReady_(false),
      trayCallbackMessage_(kTrayCallbackMessage),
      taskbarCreatedMessage_(RegisterWindowMessageW(L"TaskbarCreated")),
      desktopHealthTimerId_(kDesktopHealthTimerId),
      desktopHealthIntervalMs_(kDesktopHealthIntervalMs),
      currentDpi_(kDefaultDpi),
      uiFont_(nullptr),
      titleFont_(nullptr),
      isPinned_(false),
      isPaused_(false),
      isExiting_(false),
      isDesktopConnected_(false) {}

AppController::~AppController() {
    mouseController_.Stop();
    if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
        KillTimer(mainWindow_, desktopHealthTimerId_);
    }
    trayIcon_.Remove();
    if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
        DestroyWindow(mainWindow_);
        mainWindow_ = nullptr;
    }
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
        uiFont_ = nullptr;
    }
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
        titleFont_ = nullptr;
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
    // Overlay runs as an independent top-level window and should not be owned by main window.
    if (!overlayWindow_.Initialize(instance_, nullptr)) {
        Infrastructure::Logger::Get().Error(L"[Overlay] initialization failed.");
    } else {
        overlayWindow_.SetSelectionConfirmCallback(
            [this](bool confirmed) { HandleSelectionConfirmDecision(confirmed); });
        UpdateOverlayWindow();
        overlayWindow_.Show();
    }
    if (!InitializePersistence()) {
        Infrastructure::Logger::Get().Error(L"Persistence initialization failed.");
    }

    mouseController_.SetCallbacks(
        [this](const POINT& startPoint) { HandleSelectionStarted(startPoint); },
        [this](const RECT& selectionRect) { HandleSelectionUpdated(selectionRect); },
        [this](const RECT& selectionRect, const POINT& releasePoint) {
            HandleSelectionCompleted(selectionRect, releasePoint);
        },
        [this]() { HandleSelectionCanceled(); });
    mouseController_.SetMouseEventFilterCallback(
        [this](WPARAM message, const POINT& point) {
            return HandleSelectionConfirmMouseFilter(message, point);
        });
    if (!mouseController_.Start()) {
        Infrastructure::Logger::Get().Error(L"[Selection] failed to install mouse hook.");
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
        desktopIconReadStatus_ = L"Persistence init failed: unable to open database.";
        return false;
    }

    if (!Persistence::EnsureSchema(database_)) {
        desktopIconReadStatus_ = L"Persistence init failed: schema migration failed.";
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
    const DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    currentDpi_ = GetDpiForWindowCompat(nullptr);
    UpdateDpiMetrics(currentDpi_);

    RECT windowRect{0, 0, ScaleForDpi(kBaseWindowWidth), ScaleForDpi(kBaseWindowHeight)};
    AdjustWindowRectForDpiCompat(&windowRect, windowStyle, FALSE, 0, currentDpi_);

    mainWindow_ = CreateWindowExW(
        0,
        kMainWindowClassName,
        L"WinIconManagement - Initialization Stage",
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
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

    currentDpi_ = GetWindowDpi();
    UpdateDpiMetrics(currentDpi_);
    SetWindowPos(
        mainWindow_,
        nullptr,
        0,
        0,
        ScaleForDpi(kMinWindowWidth),
        ScaleForDpi(kMinWindowHeight),
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

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
        ScaleForDpi(24),
        ScaleForDpi(122),
        ScaleForDpi(800),
        ScaleForDpi(460),
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

    ApplyDpiFonts();
    return true;
}

void AppController::LayoutDiagnosticsTextControl(int clientWidth, int clientHeight) {
    if (diagnosticsTextControl_ == nullptr || !IsWindow(diagnosticsTextControl_)) {
        return;
    }

    const int left = ScaleForDpi(24);
    const int top = ScaleForDpi(160);
    const int rightPadding = ScaleForDpi(24);
    const int bottomPadding = ScaleForDpi(24);
    const int minWidth = ScaleForDpi(280);
    const int minHeight = ScaleForDpi(220);

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

void AppController::UpdateDpiMetrics(UINT dpi) {
    currentDpi_ = dpi == 0 ? kDefaultDpi : dpi;

    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
        uiFont_ = nullptr;
    }
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
        titleFont_ = nullptr;
    }

    LOGFONTW bodyFont{};
    bodyFont.lfHeight = -MulDiv(18, static_cast<int>(currentDpi_), static_cast<int>(kDefaultDpi));
    bodyFont.lfWeight = FW_NORMAL;
    bodyFont.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(bodyFont.lfFaceName, LF_FACESIZE, L"Microsoft YaHei UI");
    uiFont_ = CreateFontIndirectW(&bodyFont);

    LOGFONTW headingFont = bodyFont;
    headingFont.lfHeight = -MulDiv(30, static_cast<int>(currentDpi_), static_cast<int>(kDefaultDpi));
    headingFont.lfWeight = FW_SEMIBOLD;
    titleFont_ = CreateFontIndirectW(&headingFont);
}

void AppController::ApplyDpiFonts() {
    if (diagnosticsTextControl_ != nullptr && IsWindow(diagnosticsTextControl_)) {
        SendMessageW(
            diagnosticsTextControl_,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(uiFont_ != nullptr ? uiFont_ : GetStockObject(DEFAULT_GUI_FONT)),
            TRUE);
    }
}

int AppController::ScaleForDpi(int value) const {
    return MulDiv(value, static_cast<int>(currentDpi_), static_cast<int>(kDefaultDpi));
}

UINT AppController::GetWindowDpi() const {
    return GetDpiForWindowCompat(mainWindow_);
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

    if (desktopResolveResult_.overlayAnchorWindow != nullptr &&
        IsWindow(desktopResolveResult_.overlayAnchorWindow)) {
        overlayWindow_.SetDesktopHostWindow(desktopResolveResult_.overlayAnchorWindow);
    } else if (desktopResolveResult_.workerWindow != nullptr && IsWindow(desktopResolveResult_.workerWindow)) {
        overlayWindow_.SetDesktopHostWindow(desktopResolveResult_.workerWindow);
    } else if (desktopResolveResult_.progmanWindow != nullptr &&
               IsWindow(desktopResolveResult_.progmanWindow)) {
        overlayWindow_.SetDesktopHostWindow(desktopResolveResult_.progmanWindow);
    } else {
        overlayWindow_.SetDesktopHostWindow(nullptr);
    }

    const DisplayDiagnostics display = CollectDisplayDiagnostics();
    overlayWindow_.SetVirtualDesktopRect(display.virtualDesktopRect);

    const RECT layoutBaseRect = display.hasPrimaryMonitor
                                    ? display.primaryMonitorRect
                                    : display.virtualDesktopRect;
    RECT fenceRect{};
    const int width = layoutBaseRect.right - layoutBaseRect.left;
    const int height = layoutBaseRect.bottom - layoutBaseRect.top;
    const int fenceWidth = std::max(280, width / 4);
    const int fenceHeight = std::max(180, height / 4);
    fenceRect.left = layoutBaseRect.left + std::max(48, width / 8);
    fenceRect.top = layoutBaseRect.top + std::max(48, height / 8);
    fenceRect.right = fenceRect.left + fenceWidth;
    fenceRect.bottom = fenceRect.top + fenceHeight;

    overlayWindow_.SetFenceRect(fenceRect);
    overlayWindow_.SetFixedMode(!isPaused_);
    Infrastructure::Logger::Get().Info(
        L"[Overlay] UpdateOverlayWindow base=" +
        std::wstring(display.hasPrimaryMonitor ? L"primary" : L"virtual") +
        L"; baseRect=" + RectToString(layoutBaseRect) +
        L"; fenceRect=" + RectToString(fenceRect));
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
        case WM_APP + 100:
            hasPendingSelectionRect_ = false;
            overlayWindow_.ClearSelectionRect();
            return 0;
        case WM_APP + 101: {
            const auto* selectionRect = reinterpret_cast<RECT*>(lParam);
            if (selectionRect != nullptr) {
                overlayWindow_.SetSelectionRect(*selectionRect);
                delete selectionRect;
            }
            return 0;
        }
        case WM_APP + 102: {
            const auto* payload = reinterpret_cast<SelectionCompletePayload*>(lParam);
            if (payload != nullptr) {
                const RECT completedRect = payload->rect;
                const POINT releasePoint = payload->releasePoint;
                delete payload;
                ConfirmSelectionRect(completedRect, releasePoint);
            }
            return 0;
        }
        case WM_APP + 103:
            hasPendingSelectionRect_ = false;
            overlayWindow_.ClearSelectionRect();
            return 0;
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
            currentDpi_ = HIWORD(wParam);
            if (currentDpi_ == 0) {
                currentDpi_ = GetWindowDpi();
            }
            UpdateDpiMetrics(currentDpi_);
            ApplyDpiFonts();
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
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            LayoutDiagnosticsTextControl(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
            InvalidateRect(hwnd, nullptr, TRUE);
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
                    L"Batch move failed. Please check the log.",
                    L"WinIconManagement",
                    MB_OK | MB_ICONWARNING);
            }
            break;
        case IDM_TRAY_RESTORE_LAYOUT:
            Infrastructure::Logger::Get().Info(L"Tray command: Restore layout.");
            if (!RestoreOriginalDesktopLayout()) {
                MessageBoxW(
                    hwnd,
                    L"Restore layout failed. Please run batch grid move once and check the log.",
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

    HGDIOBJ previousFont = SelectObject(
        deviceContext,
        titleFont_ != nullptr ? titleFont_ : GetStockObject(DEFAULT_GUI_FONT));

    RECT titleRect{
        ScaleForDpi(24),
        ScaleForDpi(24),
        clientRect.right - ScaleForDpi(24),
        ScaleForDpi(72)};
    DrawTextW(deviceContext, L"WinIconManagement", -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(deviceContext, uiFont_ != nullptr ? uiFont_ : GetStockObject(DEFAULT_GUI_FONT));
    RECT labelRect{
        ScaleForDpi(24),
        ScaleForDpi(84),
        clientRect.right - ScaleForDpi(24),
        ScaleForDpi(120)};
    DrawTextW(deviceContext, L"Desktop Connection Status", -1, &labelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const COLORREF statusColor = isDesktopConnected_ ? RGB(40, 167, 69) : RGB(220, 53, 69);
    HBRUSH statusBrush = CreateSolidBrush(statusColor);
    HPEN statusPen = CreatePen(PS_SOLID, 1, statusColor);
    HGDIOBJ previousBrush = SelectObject(deviceContext, statusBrush);
    HGDIOBJ previousPen = SelectObject(deviceContext, statusPen);
    const int statusLeft = ScaleForDpi(24);
    const int statusTop = ScaleForDpi(130);
    const int statusSize = ScaleForDpi(16);
    Ellipse(deviceContext, statusLeft, statusTop, statusLeft + statusSize, statusTop + statusSize);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);
    DeleteObject(statusPen);
    DeleteObject(statusBrush);

    const wchar_t* statusText = isDesktopConnected_ ? L"Connected to desktop" : L"Desktop connection failed";
    RECT statusRect{
        ScaleForDpi(48),
        ScaleForDpi(122),
        clientRect.right - ScaleForDpi(24),
        ScaleForDpi(150)};
    DrawTextW(deviceContext, statusText, -1, &statusRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(deviceContext, previousFont);
    EndPaint(hwnd, &paintStruct);
}

void AppController::ResolveDesktopWindows(bool fromManualReconnect) {
    desktopResolveResult_ = desktopResolver_.Resolve();
    isDesktopConnected_ = desktopResolveResult_.success &&
                          Desktop::DesktopWindowResolver::IsWindowChainValid(desktopResolveResult_);

    LogDesktopResolveDiagnostics();
    mouseController_.SetDesktopListViewWindow(desktopResolveResult_.listViewWindow);
    mouseController_.SetEnabled(false);

    if (isDesktopConnected_) {
        mouseController_.SetEnabled(true);
        RefreshDesktopIconSnapshot();
        UpdateOverlayWindow();
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
            L"Reconnect desktop failed. Please check the log and try again.",
            L"WinIconManagement",
            MB_OK | MB_ICONWARNING);
    }

    if (mainWindow_ != nullptr && IsWindow(mainWindow_)) {
        InvalidateRect(mainWindow_, nullptr, TRUE);
    }
    UpdateOverlayWindow();
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
        desktopIconReadStatus_ = L"Batch move failed: desktop icons unavailable.";
        lastGridMoveSummary_ = L"Failed: desktop icons unavailable.";
        UpdateDiagnosticsTextControl();
        return false;
    }

    CacheOriginalIconPositions();
    const DisplayDiagnostics display = CollectDisplayDiagnostics();
    GridMovePlan plan{};
    if (!BuildGridMovePlan(desktopIcons_, display.virtualDesktopRect, &plan) ||
        plan.iconsToMove.empty()) {
        desktopIconReadStatus_ = L"Batch move failed: unable to compute visible grid targets.";
        lastGridMoveSummary_ = L"Failed: unable to compute visible grid targets.";
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
        desktopIconReadStatus_ = L"Batch move failed: planned displacement is too small.";
        lastGridMoveSummary_ = L"Failed: planned displacement is too small.";
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
        desktopIconReadStatus_ = L"Batch move failed: no icons were moved.";
        lastGridMoveSummary_ = L"Failed: " + lastGridMoveSummary_;
        UpdateDiagnosticsTextControl();
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[DesktopMove] bulk grid summary: " + lastGridMoveSummary_);
    RefreshDesktopIconSnapshot();
    PersistIconSnapshot(L"after_batch_move", L"auto");
    if (movedCount == expected) {
        desktopIconReadStatus_ = L"Batch move completed: " +
                                 std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
    } else {
        desktopIconReadStatus_ = L"Batch move partially completed: " +
                                 std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
    }
    UpdateDiagnosticsTextControl();
    return movedCount > 0;
}

bool AppController::RestoreOriginalDesktopLayout() {
    if (!EnsureDesktopConnection()) {
        desktopIconReadStatus_ = L"Restore failed: desktop connection unavailable.";
        lastGridMoveSummary_ = L"Restore failed: desktop connection unavailable.";
        UpdateDiagnosticsTextControl();
        return false;
    }
    if (originalDesktopIcons_.empty()) {
        desktopIconReadStatus_ = L"Restore failed: no original snapshot available.";
        lastGridMoveSummary_ = L"Restore failed: no original snapshot available.";
        Infrastructure::Logger::Get().Error(L"[DesktopMove] restore skipped: no original snapshot.");
        UpdateDiagnosticsTextControl();
        return false;
    }

    // Reconnect before restore to avoid stale handles after Explorer restarts.
    ResolveDesktopWindows(false);
    if (!isDesktopConnected_ ||
        !Desktop::DesktopWindowResolver::IsWindowChainValid(desktopResolveResult_)) {
        desktopIconReadStatus_ = L"Restore failed: desktop handle invalid after reconnect.";
        lastGridMoveSummary_ = L"Restore failed: desktop handle invalid.";
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
        desktopIconReadStatus_ = L"Restore completed.";
        lastGridMoveSummary_ = L"Restore completed: " +
                               std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
    } else {
        desktopIconReadStatus_ = L"Restore partially completed: " +
                                 std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
        lastGridMoveSummary_ = L"Restore partially completed: " +
                               std::to_wstring(movedCount) + L"/" + std::to_wstring(expected);
    }
    UpdateDiagnosticsTextControl();
    return movedCount > 0;
}

void AppController::HandleSelectionStarted(const POINT&) {
    if (mainWindow_ == nullptr || !IsWindow(mainWindow_)) {
        return;
    }
    Infrastructure::Logger::Get().Info(L"[Selection] started.");
    PostMessageW(mainWindow_, WM_APP + 100, 0, 0);
}

void AppController::HandleSelectionUpdated(const RECT& selectionRect) {
    if (mainWindow_ == nullptr || !IsWindow(mainWindow_)) {
        return;
    }
    static ULONGLONG s_lastSelectionUpdateLogTick = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - s_lastSelectionUpdateLogTick >= 250) {
        s_lastSelectionUpdateLogTick = now;
        Infrastructure::Logger::Get().Info(
            L"[Selection] updating rect: [" +
            std::to_wstring(selectionRect.left) + L"," +
            std::to_wstring(selectionRect.top) + L"]-[" +
            std::to_wstring(selectionRect.right) + L"," +
            std::to_wstring(selectionRect.bottom) + L"]");
    }
    auto* rectCopy = new RECT(selectionRect);
    PostMessageW(mainWindow_, WM_APP + 101, 0, reinterpret_cast<LPARAM>(rectCopy));
}

void AppController::HandleSelectionCompleted(const RECT& selectionRect, const POINT& releasePoint) {
    if (mainWindow_ == nullptr || !IsWindow(mainWindow_)) {
        return;
    }
    Infrastructure::Logger::Get().Info(
        L"[Selection] completed rect: [" +
        std::to_wstring(selectionRect.left) + L"," +
        std::to_wstring(selectionRect.top) + L"]-[" +
        std::to_wstring(selectionRect.right) + L"," +
        std::to_wstring(selectionRect.bottom) + L"]");
    auto* payload = new SelectionCompletePayload{};
    payload->rect = selectionRect;
    payload->releasePoint = releasePoint;
    PostMessageW(mainWindow_, WM_APP + 102, 0, reinterpret_cast<LPARAM>(payload));
}

void AppController::HandleSelectionCanceled() {
    if (mainWindow_ == nullptr || !IsWindow(mainWindow_)) {
        return;
    }
    Infrastructure::Logger::Get().Info(L"[Selection] canceled.");
    PostMessageW(mainWindow_, WM_APP + 103, 0, 0);
}

bool AppController::HandleSelectionConfirmMouseFilter(WPARAM message, const POINT& point) {
    if (!overlayWindow_.IsSelectionConfirmVisible()) {
        return false;
    }

    const bool isPointInConfirm = overlayWindow_.IsPointInSelectionConfirm(point);
    switch (message) {
        case WM_MOUSEMOVE: {
            const bool confirmHandled = overlayWindow_.HandleSelectionConfirmClick(message, point);
            (void)confirmHandled;
            return false;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            if (isPointInConfirm) {
                return overlayWindow_.HandleSelectionConfirmClick(message, point);
            }
            CancelSelectionRect();
            return false;
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            CancelSelectionRect();
            return false;
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            return false;
        default:
            return false;
    }
}

void AppController::ConfirmSelectionRect(const RECT& selectionRect, const POINT& anchorPoint) {
    const int width = selectionRect.right - selectionRect.left;
    const int height = selectionRect.bottom - selectionRect.top;
    if (width < kSelectionMinWidth || height < kSelectionMinHeight) {
        desktopIconReadStatus_ = L"Selection canceled: area too small.";
        hasPendingSelectionRect_ = false;
        overlayWindow_.ClearSelectionRect();
        UpdateDiagnosticsTextControl();
        return;
    }

    pendingSelectionRect_ = selectionRect;
    hasPendingSelectionRect_ = true;
    overlayWindow_.ShowSelectionConfirm(selectionRect, anchorPoint);
}

void AppController::HandleSelectionConfirmDecision(bool confirmed) {
    if (!hasPendingSelectionRect_) {
        return;
    }

    const RECT selectionRect = pendingSelectionRect_;
    hasPendingSelectionRect_ = false;
    if (confirmed) {
        ApplyFenceFromSelectionRect(selectionRect);
        return;
    }
    CancelSelectionRect();
}

void AppController::CancelSelectionRect() {
    overlayWindow_.ClearSelectionRect();
    hasPendingSelectionRect_ = false;
    desktopIconReadStatus_ = L"Selection canceled.";
    UpdateDiagnosticsTextControl();
}

void AppController::ApplyFenceFromSelectionRect(const RECT& selectionRect) {
    overlayWindow_.ClearSelectionRect();

    if (!EnsureDesktopConnection()) {
        desktopIconReadStatus_ = L"Selection failed: desktop connection unavailable.";
        UpdateDiagnosticsTextControl();
        return;
    }

    RefreshDesktopIconSnapshot();
    const RECT fenceRect = BuildFenceRectFromSelection(selectionRect);
    const std::vector<Desktop::DesktopIcon> selectedIcons = CollectIconsInRect(selectionRect);
    const std::vector<Desktop::DesktopIcon> movedIcons =
        BuildIconsForFenceLayout(selectedIcons, fenceRect);

    int movedCount = 0;
    if (!movedIcons.empty()) {
        movedCount = desktopIconService_.MoveDesktopIcons(
            desktopResolveResult_.listViewWindow,
            desktopResolveResult_.explorerProcessId,
            movedIcons);
    }

    overlayWindow_.SetFenceRect(fenceRect);
    if (!selectedIcons.empty() && !movedIcons.empty()) {
        SaveFenceSelection(fenceRect, selectedIcons, movedIcons);
    } else if (persistenceReady_) {
        // Keep empty rectangle as a valid fence even when no icons were hit.
        Persistence::FenceRecord fence{};
        fence.name = L"Desktop Group";
        fence.bounds = fenceRect;
        fence.styleJson = L"{\"source\":\"drag-selection\",\"empty\":true}";
        const long long fenceId = fenceRepository_.CreateFence(fence);
        if (fenceId > 0) {
            (void)fenceRepository_.ReplaceFenceIcons(fenceId, {});
        }
    }

    RefreshDesktopIconSnapshot();
    if (selectedIcons.empty()) {
        desktopIconReadStatus_ = L"Selection applied: rectangle saved (no icons in region).";
    } else if (movedCount > 0) {
        desktopIconReadStatus_ =
            L"Selection grouped icons: " + std::to_wstring(movedCount) +
            L"/" + std::to_wstring(movedIcons.size());
    } else {
        desktopIconReadStatus_ = L"Selection applied: rectangle saved, icon move skipped.";
    }
    UpdateDiagnosticsTextControl();
}

std::vector<Desktop::DesktopIcon> AppController::CollectIconsInRect(const RECT& selectionRect) const {
    std::vector<Desktop::DesktopIcon> selected;
    if (desktopIcons_.empty()) {
        return selected;
    }

    for (const Desktop::DesktopIcon& icon : desktopIcons_) {
        if (icon.position.x >= selectionRect.left && icon.position.x <= selectionRect.right &&
            icon.position.y >= selectionRect.top && icon.position.y <= selectionRect.bottom) {
            selected.push_back(icon);
        }
    }
    return selected;
}

RECT AppController::BuildFenceRectFromSelection(const RECT& selectionRect) const {
    RECT fenceRect = selectionRect;
    if (fenceRect.left > fenceRect.right) {
        std::swap(fenceRect.left, fenceRect.right);
    }
    if (fenceRect.top > fenceRect.bottom) {
        std::swap(fenceRect.top, fenceRect.bottom);
    }

    if ((fenceRect.right - fenceRect.left) < 180) {
        fenceRect.right = fenceRect.left + 180;
    }
    if ((fenceRect.bottom - fenceRect.top) < 140) {
        fenceRect.bottom = fenceRect.top + 140;
    }

    const DisplayDiagnostics display = CollectDisplayDiagnostics();
    fenceRect.left = std::max(fenceRect.left, display.virtualDesktopRect.left);
    fenceRect.top = std::max(fenceRect.top, display.virtualDesktopRect.top);
    fenceRect.right = std::min(fenceRect.right, display.virtualDesktopRect.right);
    fenceRect.bottom = std::min(fenceRect.bottom, display.virtualDesktopRect.bottom);
    return fenceRect;
}

std::vector<Desktop::DesktopIcon> AppController::BuildIconsForFenceLayout(
    const std::vector<Desktop::DesktopIcon>& selectedIcons,
    const RECT& fenceRect) const {
    std::vector<Desktop::DesktopIcon> moved = selectedIcons;
    if (moved.empty()) {
        return moved;
    }

    const int fenceWidth = static_cast<int>(fenceRect.right - fenceRect.left);
    const int fenceHeight = static_cast<int>(fenceRect.bottom - fenceRect.top);
    const int availableWidth = std::max(1, fenceWidth - kFenceInnerPadding * 2);
    const int availableHeight = std::max(1, fenceHeight - kFenceInnerPadding * 2);
    const int spacingX = std::max(GetSystemMetrics(SM_CXICONSPACING), kMinimumGridSpacing);
    const int spacingY = std::max(GetSystemMetrics(SM_CYICONSPACING), kMinimumGridSpacing);
    const int columns = std::max(1, availableWidth / spacingX);
    const int rows = std::max(1, availableHeight / spacingY);
    const int capacity = columns * rows;
    const int count = static_cast<int>(moved.size());
    const int layoutCount = std::min(count, capacity);

    std::sort(
        moved.begin(),
        moved.end(),
        [](const Desktop::DesktopIcon& a, const Desktop::DesktopIcon& b) {
            if (a.position.y != b.position.y) {
                return a.position.y < b.position.y;
            }
            if (a.position.x != b.position.x) {
                return a.position.x < b.position.x;
            }
            return a.index < b.index;
        });

    const int startX = fenceRect.left + kFenceInnerPadding;
    const int startY = fenceRect.top + kFenceInnerPadding;
    for (int i = 0; i < layoutCount; ++i) {
        const int row = i / columns;
        const int column = i % columns;
        moved[static_cast<size_t>(i)].position.x = startX + column * spacingX;
        moved[static_cast<size_t>(i)].position.y = startY + row * spacingY;
    }

    if (layoutCount < count) {
        moved.resize(static_cast<size_t>(layoutCount));
    }
    return moved;
}

void AppController::SaveFenceSelection(
    const RECT& fenceRect,
    const std::vector<Desktop::DesktopIcon>& originalIcons,
    const std::vector<Desktop::DesktopIcon>& movedIcons) {
    if (!persistenceReady_) {
        return;
    }

    Persistence::FenceRecord fence{};
    fence.name = L"Desktop Group";
    fence.bounds = fenceRect;
    fence.styleJson = L"{\"source\":\"drag-selection\"}";
    const long long fenceId = fenceRepository_.CreateFence(fence);
    if (fenceId <= 0) {
        Infrastructure::Logger::Get().Error(L"[Selection] failed to persist fence.");
        return;
    }

    std::unordered_map<int, Desktop::DesktopIcon> originalByIndex;
    originalByIndex.reserve(originalIcons.size());
    for (const Desktop::DesktopIcon& icon : originalIcons) {
        originalByIndex.emplace(icon.index, icon);
    }

    std::vector<Persistence::FenceIconRecord> rows;
    rows.reserve(movedIcons.size());
    for (size_t i = 0; i < movedIcons.size(); ++i) {
        const Desktop::DesktopIcon& moved = movedIcons[i];
        const auto found = originalByIndex.find(moved.index);
        if (found == originalByIndex.end()) {
            continue;
        }
        Persistence::FenceIconRecord row{};
        row.fenceId = fenceId;
        row.orderIndex = static_cast<int>(i);
        row.iconIdentity = Persistence::BuildIconIdentity(found->second);
        row.iconName = found->second.displayName;
        row.currentX = moved.position.x;
        row.currentY = moved.position.y;
        row.originalX = found->second.position.x;
        row.originalY = found->second.position.y;
        rows.push_back(std::move(row));
    }

    if (!fenceRepository_.ReplaceFenceIcons(fenceId, rows)) {
        Infrastructure::Logger::Get().Error(L"[Selection] failed to persist fence icons.");
        return;
    }

    Infrastructure::Logger::Get().Info(
        L"[Selection] fence persisted. id=" + std::to_wstring(fenceId) +
        L"; iconCount=" + std::to_wstring(rows.size()) +
        L"; rect=" + RectToString(fenceRect));
}

void AppController::LogDesktopResolveDiagnostics() const {
    std::wstring summary = L"[DesktopResolve] success=";
    summary += isDesktopConnected_ ? L"true" : L"false";
    summary += L"; path=" + desktopResolveResult_.resolvePath;
    summary += L"; fallback=" + std::wstring(desktopResolveResult_.usedEnumWindowsFallback ? L"true" : L"false");
    summary += L"; progman=" + HandleToString(desktopResolveResult_.progmanWindow);
    summary += L"; worker=" + HandleToString(desktopResolveResult_.workerWindow);
    summary += L"; workerAfterDefView=" + HandleToString(desktopResolveResult_.workerWindowAfterDefView);
    summary += L"; overlayAnchor=" + HandleToString(desktopResolveResult_.overlayAnchorWindow);
    summary += L"; overlayAnchorStrategy=" + desktopResolveResult_.overlayAnchorStrategy;
    summary += L"; defView=" + HandleToString(desktopResolveResult_.shellDefViewWindow);
    summary += L"; listView=" + HandleToString(desktopResolveResult_.listViewWindow);
    summary += L"; explorerPid=" + std::to_wstring(desktopResolveResult_.explorerProcessId);
    summary += L"; progmanClass=" + desktopResolveResult_.progmanClassName;
    summary += L"; workerClass=" + desktopResolveResult_.workerClassName;
    summary += L"; workerAfterDefViewClass=" + desktopResolveResult_.workerAfterDefViewClassName;
    summary += L"; overlayAnchorClass=" + desktopResolveResult_.overlayAnchorClassName;
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
    text += L"Resolve Path:\r\n";
    text += L"  " + wrappedPath + L"\r\n";
    text += L"Fallback: " + std::wstring(desktopResolveResult_.usedEnumWindowsFallback ? L"true" : L"false") + L"\r\n";
    text += L"Progman: " + HandleToString(desktopResolveResult_.progmanWindow) +
            L" (" + desktopResolveResult_.progmanClassName + L")\r\n";
    text += L"WorkerW: " + HandleToString(desktopResolveResult_.workerWindow) +
            L" (" + desktopResolveResult_.workerClassName + L")\r\n";
    text += L"WorkerW(AfterDefView): " + HandleToString(desktopResolveResult_.workerWindowAfterDefView) +
            L" (" + desktopResolveResult_.workerAfterDefViewClassName + L")\r\n";
    text += L"Overlay Anchor: " + HandleToString(desktopResolveResult_.overlayAnchorWindow) +
            L" (" + desktopResolveResult_.overlayAnchorClassName + L")\r\n";
    text += L"Overlay Strategy: " + desktopResolveResult_.overlayAnchorStrategy + L"\r\n";
    text += L"SHELLDLL_DefView: " + HandleToString(desktopResolveResult_.shellDefViewWindow) +
            L" (" + desktopResolveResult_.shellDefViewClassName + L")\r\n";
    text += L"SysListView32: " + HandleToString(desktopResolveResult_.listViewWindow) +
            L" (" + desktopResolveResult_.listViewClassName + L")\r\n";
    text += L"Explorer PID: " + std::to_wstring(desktopResolveResult_.explorerProcessId) + L"\r\n";
    const DisplayDiagnostics display = CollectDisplayDiagnostics();
    text += L"Monitor Count: " + std::to_wstring(display.monitorCount) + L"\r\n";
    text += L"Virtual Desktop: " + RectToString(display.virtualDesktopRect) + L"\r\n";
    if (!display.monitorDetails.empty()) {
        text += L"Monitor Details:\r\n" + display.monitorDetails;
    }

    text += L"\r\nExpected Icons: " + std::to_wstring(desktopIconCount_) + L"\r\n";
    text += L"Read Icons: " + std::to_wstring(desktopIcons_.size()) + L"\r\n";
    text += L"Read Status: " + desktopIconReadStatus_ + L"\r\n";
    text += L"Batch Move Summary: " + lastGridMoveSummary_ + L"\r\n";
    text += L"Persistence: " + std::wstring(persistenceReady_ ? L"ready" : L"not ready") + L"\r\n";
    if (persistenceReady_) {
        text += L"Database Path: " + database_.DatabasePath().wstring() + L"\r\n";
    } else if (!database_.LastError().empty()) {
        text += L"Database Error: " + database_.LastError() + L"\r\n";
    }
    text += L"Original Snapshot Count: " + std::to_wstring(originalDesktopIcons_.size()) + L"\r\n";
    if (!desktopIcons_.empty()) {
        text += L"Icon Samples:\r\n";
        const size_t sampleCount = std::min<size_t>(desktopIcons_.size(), 10);
        for (size_t i = 0; i < sampleCount; ++i) {
            const Desktop::DesktopIcon& icon = desktopIcons_[i];
            text += L"  [" + std::to_wstring(icon.index) + L"] ";
            text += icon.displayName.empty() ? L"<empty>" : icon.displayName;
            text += L" @ " + PointToString(icon.position) + L"\r\n";
        }
    }

    if (!isDesktopConnected_) {
        text += L"Failure Step: " +
                (desktopResolveResult_.failureStep.empty() ? std::wstring(L"<unknown>") : desktopResolveResult_.failureStep) +
                L"\r\n";
        text += L"Failure Code: " + std::to_wstring(desktopResolveResult_.failureCode) + L"\r\n";
    }

    return text;
}
}  // namespace App
