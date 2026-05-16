#include "App/AppController.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
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
constexpr int kFenceResizeStep = 48;
constexpr int kRenameDialogWidth = 360;
constexpr int kRenameDialogHeight = 132;
constexpr int kRenameEditControlId = 5001;
constexpr int kRenamePromptControlId = 5002;
constexpr int kRenameOkButtonId = IDOK;
constexpr int kRenameCancelButtonId = IDCANCEL;

std::wstring TrimWhitespace(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && std::iswspace(value[start]) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::iswspace(value[end - 1]) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

struct RenameFenceDialogState {
    std::wstring* value = nullptr;
    std::wstring initialValue;
    bool confirmed = false;
};

INT_PTR CALLBACK RenameFenceDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<RenameFenceDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_CREATE: {
            const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<RenameFenceDialogState*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HWND prompt = CreateWindowExW(
                0,
                L"STATIC",
                L"请输入分组名称",
                WS_CHILD | WS_VISIBLE,
                16,
                14,
                kRenameDialogWidth - 32,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRenamePromptControlId)),
                createStruct->hInstance,
                nullptr);
            if (prompt != nullptr) {
                SendMessageW(prompt, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }

            HWND edit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state != nullptr ? state->initialValue.c_str() : L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                16,
                40,
                kRenameDialogWidth - 32,
                24,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRenameEditControlId)),
                createStruct->hInstance,
                nullptr);
            if (edit != nullptr) {
                SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                SetFocus(edit);
            }

            HWND okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"确定",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                kRenameDialogWidth - 180,
                78,
                72,
                26,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRenameOkButtonId)),
                createStruct->hInstance,
                nullptr);
            if (okButton != nullptr) {
                SendMessageW(okButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }

            HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"取消",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                kRenameDialogWidth - 96,
                78,
                72,
                26,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRenameCancelButtonId)),
                createStruct->hInstance,
                nullptr);
            if (cancelButton != nullptr) {
                SendMessageW(cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            return FALSE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kRenameOkButtonId: {
                    HWND edit = GetDlgItem(hwnd, kRenameEditControlId);
                    const int length = edit != nullptr ? GetWindowTextLengthW(edit) : 0;
                    std::wstring value(static_cast<size_t>(length), L'\0');
                    if (edit != nullptr && length > 0) {
                        GetWindowTextW(edit, value.data(), length + 1);
                    }
                    value = TrimWhitespace(value);
                    if (value.empty()) {
                        MessageBoxW(hwnd, L"分组名称不能为空。", L"WinIconManagement", MB_OK | MB_ICONINFORMATION);
                        if (edit != nullptr) {
                            SetFocus(edit);
                        }
                        return 0;
                    }
                    if (state != nullptr && state->value != nullptr) {
                        *state->value = value;
                        state->confirmed = true;
                    }
                    DestroyWindow(hwnd);
                    return 0;
                }
                case kRenameCancelButtonId:
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    break;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool ShowRenameFenceDialog(HINSTANCE instance, HWND ownerWindow, std::wstring* value) {
    if (instance == nullptr || value == nullptr) {
        return false;
    }

    const wchar_t kRenameDialogClassName[] = L"WinIconManagement.RenameFenceDialog";
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = RenameFenceDialogProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kRenameDialogClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        s_registered = true;
    }

    RenameFenceDialogState state{};
    state.value = value;
    state.initialValue = *value;

    RECT ownerRect{0, 0, 0, 0};
    if (ownerWindow != nullptr && IsWindow(ownerWindow)) {
        GetWindowRect(ownerWindow, &ownerRect);
    } else {
        ownerRect.right = GetSystemMetrics(SM_CXSCREEN);
        ownerRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    const int x = ownerRect.left + std::max(0L, ((ownerRect.right - ownerRect.left) - kRenameDialogWidth) / 2);
    const int y = ownerRect.top + std::max(0L, ((ownerRect.bottom - ownerRect.top) - kRenameDialogHeight) / 2);
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kRenameDialogClassName,
        L"重命名分组",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x,
        y,
        kRenameDialogWidth,
        kRenameDialogHeight,
        ownerWindow,
        nullptr,
        instance,
        &state);
    if (dialog == nullptr) {
        return false;
    }

    EnableWindow(ownerWindow, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (ownerWindow != nullptr && IsWindow(ownerWindow)) {
        EnableWindow(ownerWindow, TRUE);
        SetActiveWindow(ownerWindow);
    }
    return state.confirmed;
}

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
      managedFences_(),
      temporarySelection_(),
      pendingFenceCreation_(std::nullopt),
      fenceEditState_(),
      activeFenceId_(std::nullopt),
      desktopIconReadStatus_(L"Not started."),
      lastGridMoveSummary_(L"Not executed."),
      database_(),
      fenceRepository_(&database_),
      restoreSessionRepository_(&database_),
      settingsRepository_(&database_),
      snapshotRepository_(&database_),
      restoreSession_{},
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
      isDesktopConnected_(false),
      shouldRestoreManagedFences_(false),
      desktopControlMode_(DesktopControlMode::ExplorerDriven) {}

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
    if (!InitializePersistence()) {
        Infrastructure::Logger::Get().Error(L"Persistence initialization failed.");
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
        overlayWindow_.SetActiveFenceResizeCallback(
            [this](const RECT& updatedRect) { (void)UpdateActiveFenceBounds(updatedRect); });
        overlayWindow_.SetActiveFenceDeleteCallback(
            [this]() { (void)DeleteActiveFence(); });
        UpdateOverlayWindow();
        overlayWindow_.Show();
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
    mouseController_.SetSelectionStartFilterCallback(
        [this](const POINT& point) { return ShouldStartSelectionAt(point); });

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
    LoadBasicSettings();
    ReloadManagedFences();
    LoadActiveFenceSetting();
    LoadRestoreSession();
    PersistRuntimeRestoreSession(L"startup");
    Infrastructure::Logger::Get().Info(
        L"[Persistence] ready. dbPath=" + databasePath.wstring() +
        L"; schemaVersion=" + std::to_wstring(Persistence::kDatabaseSchemaVersion));
    return true;
}

void AppController::LoadBasicSettings() {
    if (!persistenceReady_) {
        return;
    }

    std::wstring value;
    if (settingsRepository_.TryGet(L"is_pinned", &value)) {
        isPinned_ = (value == L"1");
    }
    if (settingsRepository_.TryGet(L"is_paused", &value)) {
        isPaused_ = (value == L"1");
    }
}

void AppController::LoadRestoreSession() {
    restoreSession_ = Persistence::RestoreSessionRecord{};
    if (!persistenceReady_) {
        shouldRestoreManagedFences_ = false;
        return;
    }

    const std::optional<Persistence::RestoreSessionRecord> loaded = restoreSessionRepository_.Load();
    if (loaded.has_value()) {
        restoreSession_ = *loaded;
    } else {
        restoreSession_.id = 1;
        restoreSession_.lastExitMode = L"initial_bootstrap";
        restoreSession_.lastShutdownClean = managedFences_.empty();
        restoreSession_.lastRestoreNeeded = !managedFences_.empty();
        restoreSession_.updatedAtUtc = Persistence::UtcNowIso8601();
        if (!restoreSessionRepository_.Save(restoreSession_)) {
            Infrastructure::Logger::Get().Error(L"[Persistence] initialize restore session failed.");
        }
    }

    shouldRestoreManagedFences_ =
        (restoreSession_.lastRestoreNeeded || !restoreSession_.lastShutdownClean) &&
        !managedFences_.empty();
    Infrastructure::Logger::Get().Info(
        L"[Persistence] restore session loaded. exitMode=" + restoreSession_.lastExitMode +
        L"; clean=" + std::wstring(restoreSession_.lastShutdownClean ? L"true" : L"false") +
        L"; restoreNeeded=" + std::wstring(restoreSession_.lastRestoreNeeded ? L"true" : L"false") +
        L"; managedFenceCount=" + std::to_wstring(managedFences_.size()));
}

void AppController::PersistRuntimeRestoreSession(std::wstring_view reason) {
    if (!persistenceReady_) {
        return;
    }

    restoreSession_.id = 1;
    restoreSession_.lastExitMode = std::wstring(reason);
    restoreSession_.lastShutdownClean = false;
    restoreSession_.lastRestoreNeeded = shouldRestoreManagedFences_ && !managedFences_.empty();
    restoreSession_.updatedAtUtc = Persistence::UtcNowIso8601();
    if (!restoreSessionRepository_.Save(restoreSession_)) {
        Infrastructure::Logger::Get().Error(L"[Persistence] save runtime restore session failed.");
    }
}

void AppController::PersistCleanShutdownRestoreSession(std::wstring_view exitMode, bool restoreNeededAfterExit) {
    if (!persistenceReady_) {
        return;
    }

    restoreSession_.id = 1;
    restoreSession_.lastExitMode = std::wstring(exitMode);
    restoreSession_.lastShutdownClean = true;
    restoreSession_.lastRestoreNeeded = restoreNeededAfterExit;
    restoreSession_.updatedAtUtc = Persistence::UtcNowIso8601();
    if (!restoreSessionRepository_.Save(restoreSession_)) {
        Infrastructure::Logger::Get().Error(L"[Persistence] save clean shutdown restore session failed.");
    }
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

void AppController::ReloadManagedFences() {
    managedFences_.clear();
    if (!persistenceReady_) {
        activeFenceId_.reset();
        return;
    }

    const std::vector<Persistence::FenceRecord> fenceRecords = fenceRepository_.ListFences();
    managedFences_.reserve(fenceRecords.size());
    for (const Persistence::FenceRecord& fenceRecord : fenceRecords) {
        ManagedFenceState state{};
        state.record = fenceRecord;
        state.icons = fenceRepository_.ListFenceIcons(fenceRecord.id);
        managedFences_.push_back(std::move(state));
    }

    if (!activeFenceId_.has_value()) {
        if (!managedFences_.empty()) {
            activeFenceId_ = managedFences_.front().record.id;
        }
        return;
    }

    const auto activeFence = std::find_if(
        managedFences_.begin(),
        managedFences_.end(),
        [this](const ManagedFenceState& managedFence) {
            return managedFence.record.id == *activeFenceId_;
        });
    if (activeFence == managedFences_.end()) {
        activeFenceId_.reset();
    }
}

void AppController::LoadActiveFenceSetting() {
    if (!persistenceReady_) {
        return;
    }

    std::wstring value;
    if (!settingsRepository_.TryGet(L"active_fence_id", &value)) {
        if (!managedFences_.empty()) {
            activeFenceId_ = managedFences_.front().record.id;
        }
        return;
    }

    try {
        const long long fenceId = std::stoll(value);
        if (FindManagedFenceIndexById(fenceId).has_value()) {
            activeFenceId_ = fenceId;
            return;
        }
    } catch (...) {
    }

    if (!managedFences_.empty()) {
        activeFenceId_ = managedFences_.front().record.id;
    } else {
        activeFenceId_.reset();
    }
}

void AppController::PersistActiveFenceSetting() {
    if (!persistenceReady_) {
        return;
    }

    const std::wstring value =
        activeFenceId_.has_value() ? std::to_wstring(*activeFenceId_) : std::wstring(L"");
    if (!settingsRepository_.Upsert(L"active_fence_id", value)) {
        Infrastructure::Logger::Get().Error(L"[Persistence] save active fence setting failed.");
    }
}

std::optional<long long> AppController::FindManagedFenceIdAtPoint(const POINT& point) const {
    for (auto it = managedFences_.rbegin(); it != managedFences_.rend(); ++it) {
        if (PtInRect(&it->record.bounds, point) != FALSE) {
            return it->record.id;
        }
    }
    return std::nullopt;
}

std::optional<size_t> AppController::FindManagedFenceIndexById(long long fenceId) const {
    for (size_t i = 0; i < managedFences_.size(); ++i) {
        if (managedFences_[i].record.id == fenceId) {
            return i;
        }
    }
    return std::nullopt;
}

void AppController::SetActiveFence(std::optional<long long> fenceId) {
    if (fenceId.has_value()) {
        if (!FindManagedFenceIndexById(*fenceId).has_value()) {
            fenceId.reset();
        }
    }

    const bool changed = activeFenceId_ != fenceId;
    activeFenceId_ = fenceId;
    PersistActiveFenceSetting();
    if (!changed) {
        return;
    }

    UpdateOverlayWindow();
    UpdateDiagnosticsTextControl();
}

std::optional<AppController::ManagedFenceState> AppController::BuildSingleActiveFenceState() const {
    if (!activeFenceId_.has_value()) {
        return std::nullopt;
    }

    const std::optional<size_t> activeIndex = FindManagedFenceIndexById(*activeFenceId_);
    if (!activeIndex.has_value()) {
        return std::nullopt;
    }
    return managedFences_[*activeIndex];
}

std::vector<Desktop::DesktopIcon> AppController::BuildOriginalIconsFromManagedFences() const {
    std::vector<Desktop::DesktopIcon> icons;
    std::unordered_map<std::wstring, size_t> iconIndexByIdentity;

    for (size_t i = 0; i < desktopIcons_.size(); ++i) {
        iconIndexByIdentity.emplace(Persistence::BuildIconIdentity(desktopIcons_[i]), i);
    }

    for (const ManagedFenceState& managedFence : managedFences_) {
        for (const Persistence::FenceIconRecord& fenceIcon : managedFence.icons) {
            const auto found = iconIndexByIdentity.find(fenceIcon.iconIdentity);
            if (found == iconIndexByIdentity.end()) {
                continue;
            }

            Desktop::DesktopIcon icon = desktopIcons_[found->second];
            icon.position.x = fenceIcon.originalX;
            icon.position.y = fenceIcon.originalY;
            icons.push_back(std::move(icon));
        }
    }
    return icons;
}

std::vector<Desktop::DesktopIcon> AppController::BuildManagedFenceLayoutTargets() const {
    std::vector<Desktop::DesktopIcon> icons;
    std::unordered_map<std::wstring, size_t> iconIndexByIdentity;

    for (size_t i = 0; i < desktopIcons_.size(); ++i) {
        iconIndexByIdentity.emplace(Persistence::BuildIconIdentity(desktopIcons_[i]), i);
    }

    for (const ManagedFenceState& managedFence : managedFences_) {
        for (const Persistence::FenceIconRecord& fenceIcon : managedFence.icons) {
            const auto found = iconIndexByIdentity.find(fenceIcon.iconIdentity);
            if (found == iconIndexByIdentity.end()) {
                continue;
            }

            Desktop::DesktopIcon icon = desktopIcons_[found->second];
            icon.position.x = fenceIcon.currentX;
            icon.position.y = fenceIcon.currentY;
            icons.push_back(std::move(icon));
        }
    }
    return icons;
}

bool AppController::RestoreManagedFenceLayout(bool refreshFenceStateAfterMove) {
    if (!EnsureDesktopAndIconsReady()) {
        return false;
    }
    if (managedFences_.empty()) {
        return false;
    }

    const std::vector<Desktop::DesktopIcon> iconsToMove = BuildManagedFenceLayoutTargets();
    if (iconsToMove.empty()) {
        return false;
    }

    const int movedCount = desktopIconService_.MoveDesktopIcons(
        desktopResolveResult_.listViewWindow,
        desktopResolveResult_.explorerProcessId,
        iconsToMove);

    if (movedCount > 0) {
        RefreshDesktopIconSnapshot();
        if (refreshFenceStateAfterMove) {
            ReloadManagedFences();
        }
        UpdateOverlayWindow();
    }
    return movedCount > 0;
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

void AppController::ApplyExplorerDrivenRuntimeState(bool fromReconnect) {
    mouseController_.SetDesktopListViewWindow(desktopResolveResult_.listViewWindow);
    mouseController_.SetEnabled(false);

    if (!isDesktopConnected_) {
        return;
    }

    mouseController_.SetEnabled(true);
    RefreshDesktopIconSnapshot();
    if (shouldRestoreManagedFences_ && !managedFences_.empty()) {
        const bool restoredManagedFences = RestoreManagedFenceLayout(false);
        Infrastructure::Logger::Get().Info(
            L"[ExplorerDriven] managed fence restore=" +
            std::wstring(restoredManagedFences ? L"true" : L"false") +
            L"; fromReconnect=" + std::wstring(fromReconnect ? L"true" : L"false") +
            L"; fenceCount=" + std::to_wstring(managedFences_.size()));
    } else {
        Infrastructure::Logger::Get().Info(
            L"[ExplorerDriven] desktop connected without pending fence restore. fromReconnect=" +
            std::wstring(fromReconnect ? L"true" : L"false") +
            L"; fenceCount=" + std::to_wstring(managedFences_.size()));
    }
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

RECT AppController::BuildDefaultFenceRect() const {
    const DisplayDiagnostics display = CollectDisplayDiagnostics();
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
    return fenceRect;
}

RECT AppController::GetPrimaryOverlayFenceRect() const {
    if (!managedFences_.empty()) {
        return managedFences_.front().record.bounds;
    }
    return BuildDefaultFenceRect();
}

RECT AppController::BuildFenceDeleteButtonRect(const RECT& fenceRect) const {
    return RECT{
        fenceRect.right - 26,
        fenceRect.top + 6,
        fenceRect.right - 6,
        fenceRect.top + 26};
}

RECT AppController::BuildFenceResizeHandleRect(const RECT& fenceRect) const {
    return RECT{
        fenceRect.right - 32,
        fenceRect.bottom - 32,
        fenceRect.right,
        fenceRect.bottom};
}

AppController::FenceEditHitTarget AppController::HitTestFenceEditTarget(long long fenceId, const POINT& point) const {
    const std::optional<size_t> fenceIndex = FindManagedFenceIndexById(fenceId);
    if (!fenceIndex.has_value()) {
        return FenceEditHitTarget::None;
    }

    const RECT& bounds = managedFences_[*fenceIndex].record.bounds;
    if (PtInRect(&bounds, point) == FALSE) {
        return FenceEditHitTarget::None;
    }

    const RECT deleteRect = BuildFenceDeleteButtonRect(bounds);
    if (PtInRect(&deleteRect, point) != FALSE) {
        return FenceEditHitTarget::Delete;
    }

    const RECT resizeRect = BuildFenceResizeHandleRect(bounds);
    if (PtInRect(&resizeRect, point) != FALSE) {
        return FenceEditHitTarget::Resize;
    }

    return FenceEditHitTarget::Move;
}

void AppController::ApplyFencePreviewBounds(long long fenceId, const RECT& bounds) {
    const std::optional<size_t> fenceIndex = FindManagedFenceIndexById(fenceId);
    if (!fenceIndex.has_value()) {
        return;
    }

    managedFences_[*fenceIndex].record.bounds = bounds;
    UpdateOverlayWindow();
}

void AppController::BeginFenceEditDrag(long long fenceId, FenceEditHitTarget target, const POINT& point) {
    const std::optional<size_t> fenceIndex = FindManagedFenceIndexById(fenceId);
    if (!fenceIndex.has_value()) {
        return;
    }

    fenceEditState_.active = true;
    fenceEditState_.target = target;
    fenceEditState_.fenceId = fenceId;
    fenceEditState_.anchorPoint = point;
    fenceEditState_.originalBounds = managedFences_[*fenceIndex].record.bounds;
    fenceEditState_.previewBounds = fenceEditState_.originalBounds;
    SetActiveFence(fenceId);
    Infrastructure::Logger::Get().Info(
        L"[FenceEdit] begin. id=" + std::to_wstring(fenceId) +
        L"; target=" + std::to_wstring(static_cast<int>(target)) +
        L"; point=" + PointToString(point));
}

void AppController::UpdateFenceEditDrag(const POINT& point) {
    if (!fenceEditState_.active) {
        return;
    }

    RECT updatedBounds = fenceEditState_.originalBounds;
    const int deltaX = point.x - fenceEditState_.anchorPoint.x;
    const int deltaY = point.y - fenceEditState_.anchorPoint.y;
    if (fenceEditState_.target == FenceEditHitTarget::Move) {
        const int width = updatedBounds.right - updatedBounds.left;
        const int height = updatedBounds.bottom - updatedBounds.top;
        const DisplayDiagnostics display = CollectDisplayDiagnostics();
        const LONG maxLeft = std::max(display.virtualDesktopRect.left, display.virtualDesktopRect.right - width);
        const LONG maxTop = std::max(display.virtualDesktopRect.top, display.virtualDesktopRect.bottom - height);
        updatedBounds.left = std::max(
            display.virtualDesktopRect.left,
            std::min(maxLeft, fenceEditState_.originalBounds.left + deltaX));
        updatedBounds.top = std::max(
            display.virtualDesktopRect.top,
            std::min(maxTop, fenceEditState_.originalBounds.top + deltaY));
        updatedBounds.right = updatedBounds.left + width;
        updatedBounds.bottom = updatedBounds.top + height;
    } else if (fenceEditState_.target == FenceEditHitTarget::Resize) {
        updatedBounds.right = fenceEditState_.originalBounds.right + deltaX;
        updatedBounds.bottom = fenceEditState_.originalBounds.bottom + deltaY;
    }

    fenceEditState_.previewBounds = updatedBounds;
    ApplyFencePreviewBounds(fenceEditState_.fenceId, updatedBounds);
}

void AppController::EndFenceEditDrag(bool commitChanges) {
    if (!fenceEditState_.active) {
        return;
    }

    const long long fenceId = fenceEditState_.fenceId;
    const FenceEditHitTarget target = fenceEditState_.target;
    const RECT previewBounds = fenceEditState_.previewBounds;
    const RECT originalBounds = fenceEditState_.originalBounds;
    fenceEditState_ = FenceEditState{};

    if (!commitChanges) {
        ApplyFencePreviewBounds(fenceId, originalBounds);
        return;
    }

    if (target == FenceEditHitTarget::Delete) {
        (void)DeleteActiveFence();
        return;
    }

    if (!UpdateActiveFenceBounds(previewBounds)) {
        ApplyFencePreviewBounds(fenceId, originalBounds);
    }
}

bool AppController::HandleFenceEditMouse(WPARAM message, const POINT& point) {
    if (!isPaused_ || overlayWindow_.IsSelectionConfirmVisible()) {
        return false;
    }

    if (fenceEditState_.active) {
        switch (message) {
            case WM_MOUSEMOVE:
                UpdateFenceEditDrag(point);
                return false;
            case WM_LBUTTONUP:
                EndFenceEditDrag(true);
                return true;
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                EndFenceEditDrag(false);
                return true;
            default:
                return false;
        }
    }

    if (message != WM_LBUTTONDOWN) {
        return false;
    }

    const std::optional<long long> fenceId = FindManagedFenceIdAtPoint(point);
    if (!fenceId.has_value()) {
        return false;
    }

    const FenceEditHitTarget hitTarget = HitTestFenceEditTarget(*fenceId, point);
    if (hitTarget == FenceEditHitTarget::None) {
        return false;
    }

    if (hitTarget == FenceEditHitTarget::Delete) {
        SetActiveFence(*fenceId);
        (void)DeleteActiveFence();
        return true;
    }

    BeginFenceEditDrag(*fenceId, hitTarget, point);
    return true;
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
    std::vector<RECT> overlayFenceRects;
    std::vector<std::wstring> overlayFenceTitles;
    overlayFenceRects.reserve(managedFences_.size());
    overlayFenceTitles.reserve(managedFences_.size());
    for (const ManagedFenceState& managedFence : managedFences_) {
        overlayFenceRects.push_back(managedFence.record.bounds);
        std::wstring title = managedFence.record.name.empty() ? L"Desktop Group" : managedFence.record.name;
        title += L" #" + std::to_wstring(managedFence.record.id);
        title += L" (" + std::to_wstring(managedFence.icons.size()) + L")";
        overlayFenceTitles.push_back(std::move(title));
    }

    overlayWindow_.SetFenceRects(overlayFenceRects);
    std::optional<size_t> activeFenceIndex;
    if (activeFenceId_.has_value()) {
        activeFenceIndex = FindManagedFenceIndexById(*activeFenceId_);
    }
    overlayWindow_.SetFencePresentation(overlayFenceTitles, activeFenceIndex);
    overlayWindow_.SetFixedMode(!isPaused_);
    const std::wstring primaryFenceText =
        overlayFenceRects.empty() ? std::wstring(L"none") : RectToString(overlayFenceRects.front());
    Infrastructure::Logger::Get().Info(
        L"[Overlay] UpdateOverlayWindow mode=ExplorerDriven; primaryFence=" + primaryFenceText +
        L"; managedFenceCount=" + std::to_wstring(managedFences_.size()) +
        L"; activeFence=" +
        (activeFenceId_.has_value() ? std::to_wstring(*activeFenceId_) : std::wstring(L"none")));
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
            temporarySelection_.active = false;
            pendingFenceCreation_.reset();
            overlayWindow_.ClearSelectionRect();
            return 0;
        case WM_APP + 101: {
            const auto* selectionRect = reinterpret_cast<RECT*>(lParam);
            if (selectionRect != nullptr) {
                temporarySelection_.active = true;
                temporarySelection_.rect = *selectionRect;
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
            temporarySelection_.active = false;
            pendingFenceCreation_.reset();
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
                isPaused_ ? L"Tray command: Fixed control enabled." : L"Tray command: Fixed control disabled.");
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
            if (!RestoreOriginalDesktopLayout(false)) {
                MessageBoxW(
                    hwnd,
                    L"Restore layout failed. Please run batch grid move once and check the log.",
                    L"WinIconManagement",
                    MB_OK | MB_ICONWARNING);
            }
            break;
        case IDM_TRAY_RENAME_ACTIVE_FENCE:
            Infrastructure::Logger::Get().Info(L"Tray command: Rename active fence.");
            (void)RenameActiveFence(hwnd);
            break;
        case IDM_TRAY_DELETE_ACTIVE_FENCE:
            Infrastructure::Logger::Get().Info(L"Tray command: Delete active fence.");
            if (!DeleteActiveFence()) {
                MessageBoxW(hwnd, L"删除当前分组失败。", L"WinIconManagement", MB_OK | MB_ICONWARNING);
            }
            break;
        case IDM_TRAY_RESIZE_ACTIVE_FENCE_LARGER:
            Infrastructure::Logger::Get().Info(L"Tray command: Resize active fence larger.");
            if (!ResizeActiveFence(kFenceResizeStep, kFenceResizeStep)) {
                MessageBoxW(hwnd, L"放大当前分组失败。", L"WinIconManagement", MB_OK | MB_ICONWARNING);
            }
            break;
        case IDM_TRAY_RESIZE_ACTIVE_FENCE_SMALLER:
            Infrastructure::Logger::Get().Info(L"Tray command: Resize active fence smaller.");
            if (!ResizeActiveFence(-kFenceResizeStep, -kFenceResizeStep)) {
                MessageBoxW(hwnd, L"缩小当前分组失败。", L"WinIconManagement", MB_OK | MB_ICONWARNING);
            }
            break;
        case IDM_TRAY_EXIT:
        {
            isExiting_ = true;
            Infrastructure::Logger::Get().Info(L"Tray command: Exit.");
            const bool restoreNeededAfterExit = !managedFences_.empty();
            if (!RestoreOriginalDesktopLayout(true)) {
                Infrastructure::Logger::Get().Error(L"[Exit] restore original desktop layout failed before shutdown.");
            }
            PersistCleanShutdownRestoreSession(L"clean_exit", restoreNeededAfterExit);
            DestroyWindow(hwnd);
            break;
        }
        default:
            break;
    }
}

void AppController::UpdateWindowTitle() {
    std::wstring title = L"WinIconManagement | ";
    title += isPinned_ ? L"Pinned" : L"Unpinned";
    title += L" | ";
    title += isPaused_ ? L"ControlFixedOff" : L"ControlFixedOn";
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

    if (isDesktopConnected_) {
        ApplyExplorerDrivenRuntimeState(fromManualReconnect);
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

bool AppController::RestoreOriginalDesktopLayout(bool keepManagedFencesForNextLaunch) {
    if (!EnsureDesktopConnection()) {
        desktopIconReadStatus_ = L"Restore failed: desktop connection unavailable.";
        lastGridMoveSummary_ = L"Restore failed: desktop connection unavailable.";
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

    RefreshDesktopIconSnapshot();
    std::vector<Desktop::DesktopIcon> originalTargets = BuildOriginalIconsFromManagedFences();
    if (originalTargets.empty()) {
        originalTargets = originalDesktopIcons_;
    }
    if (originalTargets.empty()) {
        desktopIconReadStatus_ = L"Restore failed: no original snapshot available.";
        lastGridMoveSummary_ = L"Restore failed: no original snapshot available.";
        Infrastructure::Logger::Get().Error(L"[DesktopMove] restore skipped: no original snapshot.");
        UpdateDiagnosticsTextControl();
        return false;
    }

    const int movedCount = desktopIconService_.MoveDesktopIcons(
        desktopResolveResult_.listViewWindow,
        desktopResolveResult_.explorerProcessId,
        originalTargets);
    const int expected = static_cast<int>(originalTargets.size());

    Infrastructure::Logger::Get().Info(
        L"[DesktopMove] restore result. moved=" + std::to_wstring(movedCount) +
        L"; expected=" + std::to_wstring(expected) +
        L"; explorerPid=" + std::to_wstring(desktopResolveResult_.explorerProcessId) +
        L"; listView=" + HandleToString(desktopResolveResult_.listViewWindow));

    RefreshDesktopIconSnapshot();
    PersistIconSnapshot(L"after_restore", L"auto");
    if (!keepManagedFencesForNextLaunch) {
        if (!managedFences_.empty() && !fenceRepository_.DeleteAllFences()) {
            Infrastructure::Logger::Get().Error(L"[Persistence] delete managed fences failed after restore.");
        }
        ReloadManagedFences();
        SetActiveFence(std::nullopt);
        shouldRestoreManagedFences_ = false;
        PersistRuntimeRestoreSession(L"manual_restore");
    }

    temporarySelection_.active = false;
    pendingFenceCreation_.reset();
    UpdateOverlayWindow();
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

#if 0
bool AppController::RenameActiveFence(HWND ownerWindow) {
    if (!activeFenceId_.has_value()) {
        MessageBoxW(ownerWindow, L"当前没有可重命名的分组。", L"WinIconManagement", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    const std::optional<size_t> activeIndex = FindManagedFenceIndexById(*activeFenceId_);
    if (!activeIndex.has_value()) {
        return false;
    }

    ManagedFenceState updatedFence = managedFences_[*activeIndex];
    static int renameSequence = 1;
    updatedFence.record.name = L"Desktop Group " + std::to_wstring(renameSequence++);
    if (!fenceRepository_.UpdateFence(updatedFence.record)) {
        MessageBoxW(ownerWindow, L"重命名分组失败。", L"WinIconManagement", MB_OK | MB_ICONWARNING);
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[Fence] renamed active fence. id=" + std::to_wstring(updatedFence.record.id) +
        L"; name=" + updatedFence.record.name);
    ReloadManagedFences();
    UpdateOverlayWindow();
    UpdateDiagnosticsTextControl();
    return true;
}

bool AppController::DeleteActiveFence() {
    if (!activeFenceId_.has_value()) {
        return false;
    }

    const std::optional<size_t> activeIndex = FindManagedFenceIndexById(*activeFenceId_);
    if (!activeIndex.has_value()) {
        return false;
    }

    const ManagedFenceState fenceToDelete = managedFences_[*activeIndex];
    if (!EnsureDesktopConnection()) {
        return false;
    }

    RefreshDesktopIconSnapshot();
    std::unordered_map<std::wstring, size_t> iconIndexByIdentity;
    for (size_t i = 0; i < desktopIcons_.size(); ++i) {
        iconIndexByIdentity.emplace(Persistence::BuildIconIdentity(desktopIcons_[i]), i);
    }

    std::vector<Desktop::DesktopIcon> iconsToRestore;
    iconsToRestore.reserve(fenceToDelete.icons.size());
    for (const Persistence::FenceIconRecord& fenceIcon : fenceToDelete.icons) {
        const auto found = iconIndexByIdentity.find(fenceIcon.iconIdentity);
        if (found == iconIndexByIdentity.end()) {
            continue;
        }

        Desktop::DesktopIcon icon = desktopIcons_[found->second];
        icon.position.x = fenceIcon.originalX;
        icon.position.y = fenceIcon.originalY;
        iconsToRestore.push_back(std::move(icon));
    }

    if (!iconsToRestore.empty()) {
        (void)desktopIconService_.MoveDesktopIcons(
            desktopResolveResult_.listViewWindow,
            desktopResolveResult_.explorerProcessId,
            iconsToRestore);
        RefreshDesktopIconSnapshot();
    }

    const long long deletedFenceId = fenceToDelete.record.id;
    if (!fenceRepository_.DeleteFence(deletedFenceId)) {
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[Fence] deleted active fence. id=" + std::to_wstring(deletedFenceId));
    SetActiveFence(std::nullopt);
    ReloadManagedFences();
    UpdateOverlayWindow();
    UpdateDiagnosticsTextControl();
    return true;
}

bool AppController::ResizeActiveFence(int deltaWidth, int deltaHeight) {
    if (!activeFenceId_.has_value()) {
        return false;
    }

    const std::optional<size_t> activeIndex = FindManagedFenceIndexById(*activeFenceId_);
    if (!activeIndex.has_value()) {
        return false;
    }

    ManagedFenceState updatedFence = managedFences_[*activeIndex];
    RECT bounds = updatedFence.record.bounds;
    bounds.right += deltaWidth;
    bounds.bottom += deltaHeight;
    updatedFence.record.bounds = BuildFenceRectFromSelection(bounds);
    if (!fenceRepository_.UpdateFence(updatedFence.record)) {
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[Fence] resized active fence. id=" + std::to_wstring(updatedFence.record.id) +
        L"; bounds=" + RectToString(updatedFence.record.bounds));
    ReloadManagedFences();
    UpdateOverlayWindow();
    UpdateDiagnosticsTextControl();
    return true;
}
#endif

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

bool AppController::ShouldStartSelectionAt(const POINT& point) {
    if (!isDesktopConnected_ ||
        desktopResolveResult_.listViewWindow == nullptr ||
        !IsWindow(desktopResolveResult_.listViewWindow) ||
        desktopResolveResult_.explorerProcessId == 0) {
        Infrastructure::Logger::Get().Info(
            L"[Selection] rejected start: desktop connection unavailable. point=" + PointToString(point));
        return false;
    }

    if (const std::optional<long long> fenceId = FindManagedFenceIdAtPoint(point); fenceId.has_value()) {
        SetActiveFence(*fenceId);
        Infrastructure::Logger::Get().Info(
            L"[Selection] ignored new selection start inside managed fence. fenceId=" +
            std::to_wstring(*fenceId) +
            L"; point=" + PointToString(point));
        return false;
    }

    SetActiveFence(std::nullopt);
    const int hitIconIndex = desktopIconService_.HitTestDesktopIcon(
        desktopResolveResult_.listViewWindow,
        desktopResolveResult_.explorerProcessId,
        point);
    if (hitIconIndex >= 0) {
        Infrastructure::Logger::Get().Info(
            L"[Selection] rejected start: hit desktop icon. point=" + PointToString(point) +
            L"; iconIndex=" + std::to_wstring(hitIconIndex));
        return false;
    }
    if (hitIconIndex == -1) {
        Infrastructure::Logger::Get().Info(
            L"[Selection] accepted start: desktop blank area. point=" + PointToString(point));
        return true;
    }

    Infrastructure::Logger::Get().Info(
        L"[Selection] accepted start with hit-test fallback. point=" + PointToString(point) +
        L"; hitTestResult=" + std::to_wstring(hitIconIndex));
    return true;
}

bool AppController::HandleSelectionConfirmMouseFilter(WPARAM message, const POINT& point) {
    if (HandleFenceEditMouse(message, point)) {
        return true;
    }

    if (overlayWindow_.IsSelectionConfirmVisible()) {
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

    if (!isPaused_) {
        return false;
    }

    return false;
}

bool AppController::RenameActiveFence(HWND ownerWindow) {
    const std::optional<ManagedFenceState> activeFence = BuildSingleActiveFenceState();
    if (!activeFence.has_value()) {
        MessageBoxW(ownerWindow, L"当前没有可重命名的分组。", L"WinIconManagement", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    std::wstring updatedName =
        activeFence->record.name.empty() ? std::wstring(L"Desktop Group") : activeFence->record.name;
    if (!ShowRenameFenceDialog(instance_, ownerWindow, &updatedName)) {
        return false;
    }

    if (updatedName == activeFence->record.name) {
        return true;
    }

    Persistence::FenceRecord updatedRecord = activeFence->record;
    updatedRecord.name = updatedName;
    if (!fenceRepository_.UpdateFence(updatedRecord)) {
        MessageBoxW(ownerWindow, L"重命名分组失败。", L"WinIconManagement", MB_OK | MB_ICONWARNING);
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[Fence] renamed active fence. id=" + std::to_wstring(updatedRecord.id) +
        L"; name=" + updatedRecord.name);
    ReloadManagedFences();
    shouldRestoreManagedFences_ = !managedFences_.empty();
    PersistRuntimeRestoreSession(L"rename_fence");
    UpdateOverlayWindow();
    UpdateDiagnosticsTextControl();
    return true;
}

bool AppController::UpdateActiveFenceBounds(const RECT& bounds) {
    const std::optional<ManagedFenceState> activeFence = BuildSingleActiveFenceState();
    if (!activeFence.has_value()) {
        return false;
    }

    RECT normalizedBounds = bounds;
    if (normalizedBounds.left > normalizedBounds.right) {
        std::swap(normalizedBounds.left, normalizedBounds.right);
    }
    if (normalizedBounds.top > normalizedBounds.bottom) {
        std::swap(normalizedBounds.top, normalizedBounds.bottom);
    }

    const DisplayDiagnostics display = CollectDisplayDiagnostics();
    const int minWidth = 120;
    const int minHeight = 80;
    if ((normalizedBounds.right - normalizedBounds.left) < minWidth) {
        normalizedBounds.right = normalizedBounds.left + minWidth;
    }
    if ((normalizedBounds.bottom - normalizedBounds.top) < minHeight) {
        normalizedBounds.bottom = normalizedBounds.top + minHeight;
    }

    const int width = normalizedBounds.right - normalizedBounds.left;
    const int height = normalizedBounds.bottom - normalizedBounds.top;
    normalizedBounds.left = std::max(display.virtualDesktopRect.left, normalizedBounds.left);
    normalizedBounds.top = std::max(display.virtualDesktopRect.top, normalizedBounds.top);
    normalizedBounds.right = std::min(display.virtualDesktopRect.right, normalizedBounds.right);
    normalizedBounds.bottom = std::min(display.virtualDesktopRect.bottom, normalizedBounds.bottom);
    if ((normalizedBounds.right - normalizedBounds.left) < width) {
        normalizedBounds.left = std::max(display.virtualDesktopRect.left, normalizedBounds.right - width);
        normalizedBounds.right = normalizedBounds.left + width;
    }
    if ((normalizedBounds.bottom - normalizedBounds.top) < height) {
        normalizedBounds.top = std::max(display.virtualDesktopRect.top, normalizedBounds.bottom - height);
        normalizedBounds.bottom = normalizedBounds.top + height;
    }

    const int deltaX = normalizedBounds.left - activeFence->record.bounds.left;
    const int deltaY = normalizedBounds.top - activeFence->record.bounds.top;
    std::vector<Persistence::FenceIconRecord> updatedFenceIcons = activeFence->icons;
    for (Persistence::FenceIconRecord& fenceIcon : updatedFenceIcons) {
        fenceIcon.currentX += deltaX;
        fenceIcon.currentY += deltaY;
    }

    if ((deltaX != 0 || deltaY != 0) && !updatedFenceIcons.empty()) {
        if (!EnsureDesktopConnection()) {
            return false;
        }

        RefreshDesktopIconSnapshot();
        std::unordered_map<std::wstring, size_t> iconIndexByIdentity;
        for (size_t i = 0; i < desktopIcons_.size(); ++i) {
            iconIndexByIdentity.emplace(Persistence::BuildIconIdentity(desktopIcons_[i]), i);
        }

        std::vector<Desktop::DesktopIcon> iconsToMove;
        iconsToMove.reserve(updatedFenceIcons.size());
        for (const Persistence::FenceIconRecord& fenceIcon : updatedFenceIcons) {
            const auto found = iconIndexByIdentity.find(fenceIcon.iconIdentity);
            if (found == iconIndexByIdentity.end()) {
                continue;
            }

            Desktop::DesktopIcon icon = desktopIcons_[found->second];
            icon.position.x = fenceIcon.currentX;
            icon.position.y = fenceIcon.currentY;
            iconsToMove.push_back(std::move(icon));
        }

        if (!iconsToMove.empty()) {
            const int movedCount = desktopIconService_.MoveDesktopIcons(
                desktopResolveResult_.listViewWindow,
                desktopResolveResult_.explorerProcessId,
                iconsToMove);
            Infrastructure::Logger::Get().Info(
                L"[Fence] moved fence icons with bounds update. id=" + std::to_wstring(activeFence->record.id) +
                L"; moved=" + std::to_wstring(movedCount) +
                L"/" + std::to_wstring(iconsToMove.size()) +
                L"; delta=(" + std::to_wstring(deltaX) + L"," + std::to_wstring(deltaY) + L")");
            RefreshDesktopIconSnapshot();
        }
    }

    Persistence::FenceRecord updatedRecord = activeFence->record;
    updatedRecord.bounds = normalizedBounds;
    if (!fenceRepository_.UpdateFence(updatedRecord)) {
        Infrastructure::Logger::Get().Error(
            L"[Fence] failed to persist active fence bounds. id=" + std::to_wstring(updatedRecord.id));
        return false;
    }
    if (!updatedFenceIcons.empty() &&
        !fenceRepository_.ReplaceFenceIcons(updatedRecord.id, updatedFenceIcons)) {
        Infrastructure::Logger::Get().Error(
            L"[Fence] failed to persist moved fence icons. id=" + std::to_wstring(updatedRecord.id));
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[Fence] updated active fence bounds. id=" + std::to_wstring(updatedRecord.id) +
        L"; bounds=" + RectToString(updatedRecord.bounds));
    ReloadManagedFences();
    shouldRestoreManagedFences_ = !managedFences_.empty();
    PersistRuntimeRestoreSession(L"update_fence_bounds");
    UpdateOverlayWindow();
    UpdateDiagnosticsTextControl();
    return true;
}

bool AppController::ResizeActiveFence(int deltaWidth, int deltaHeight) {
    if (!activeFenceId_.has_value()) {
        return false;
    }

    const std::optional<size_t> activeIndex = FindManagedFenceIndexById(*activeFenceId_);
    if (!activeIndex.has_value()) {
        return false;
    }

    RECT bounds = managedFences_[*activeIndex].record.bounds;
    bounds.right += deltaWidth;
    bounds.bottom += deltaHeight;
    return UpdateActiveFenceBounds(bounds);
}

bool AppController::DeleteActiveFence() {
    if (!activeFenceId_.has_value()) {
        return false;
    }

    const std::optional<size_t> activeIndex = FindManagedFenceIndexById(*activeFenceId_);
    if (!activeIndex.has_value()) {
        return false;
    }

    const ManagedFenceState fenceToDelete = managedFences_[*activeIndex];
    if (!EnsureDesktopConnection()) {
        return false;
    }

    RefreshDesktopIconSnapshot();
    std::unordered_map<std::wstring, size_t> iconIndexByIdentity;
    for (size_t i = 0; i < desktopIcons_.size(); ++i) {
        iconIndexByIdentity.emplace(Persistence::BuildIconIdentity(desktopIcons_[i]), i);
    }

    std::vector<Desktop::DesktopIcon> iconsToRestore;
    iconsToRestore.reserve(fenceToDelete.icons.size());
    for (const Persistence::FenceIconRecord& fenceIcon : fenceToDelete.icons) {
        const auto found = iconIndexByIdentity.find(fenceIcon.iconIdentity);
        if (found == iconIndexByIdentity.end()) {
            continue;
        }

        Desktop::DesktopIcon icon = desktopIcons_[found->second];
        icon.position.x = fenceIcon.originalX;
        icon.position.y = fenceIcon.originalY;
        iconsToRestore.push_back(std::move(icon));
    }

    if (!iconsToRestore.empty()) {
        (void)desktopIconService_.MoveDesktopIcons(
            desktopResolveResult_.listViewWindow,
            desktopResolveResult_.explorerProcessId,
            iconsToRestore);
        RefreshDesktopIconSnapshot();
    }

    const long long deletedFenceId = fenceToDelete.record.id;
    if (!fenceRepository_.DeleteFence(deletedFenceId)) {
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[Fence] deleted active fence. id=" + std::to_wstring(deletedFenceId));
    SetActiveFence(std::nullopt);
    ReloadManagedFences();
    shouldRestoreManagedFences_ = !managedFences_.empty();
    PersistRuntimeRestoreSession(L"delete_fence");
    UpdateOverlayWindow();
    UpdateDiagnosticsTextControl();
    return true;
}

#if 0
bool AppController::RenameActiveFence(HWND ownerWindow) {
    const std::optional<ManagedFenceState> activeFence = BuildSingleActiveFenceState();
    if (!activeFence.has_value()) {
        MessageBoxW(ownerWindow, L"当前没有可重命名的分组。", L"WinIconManagement", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    std::wstring updatedName =
        activeFence->record.name.empty() ? std::wstring(L"Desktop Group") : activeFence->record.name;
    if (!ShowRenameFenceDialog(instance_, ownerWindow, &updatedName)) {
        return false;
    }

    if (updatedName == activeFence->record.name) {
        return true;
    }

    Persistence::FenceRecord updatedRecord = activeFence->record;
    updatedRecord.name = updatedName;
    if (!fenceRepository_.UpdateFence(updatedRecord)) {
        MessageBoxW(ownerWindow, L"重命名分组失败。", L"WinIconManagement", MB_OK | MB_ICONWARNING);
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[Fence] renamed active fence. id=" + std::to_wstring(updatedRecord.id) +
        L"; name=" + updatedRecord.name);
    ReloadManagedFences();
    UpdateOverlayWindow();
    UpdateDiagnosticsTextControl();
    return true;
}
#endif

void AppController::ConfirmSelectionRect(const RECT& selectionRect, const POINT& anchorPoint) {
    const int width = selectionRect.right - selectionRect.left;
    const int height = selectionRect.bottom - selectionRect.top;
    if (width < kSelectionMinWidth || height < kSelectionMinHeight) {
        desktopIconReadStatus_ = L"Selection canceled: area too small.";
        temporarySelection_.active = false;
        pendingFenceCreation_.reset();
        overlayWindow_.ClearSelectionRect();
        UpdateDiagnosticsTextControl();
        return;
    }

    temporarySelection_.active = false;
    pendingFenceCreation_ = PendingFenceCreationState{
        selectionRect,
        anchorPoint};
    overlayWindow_.ShowSelectionConfirm(selectionRect, anchorPoint);
}

void AppController::HandleSelectionConfirmDecision(bool confirmed) {
    if (!pendingFenceCreation_.has_value()) {
        return;
    }

    const RECT selectionRect = pendingFenceCreation_->selectionRect;
    pendingFenceCreation_.reset();
    if (confirmed) {
        ApplyFenceFromSelectionRect(selectionRect);
        return;
    }
    CancelSelectionRect();
}

void AppController::CancelSelectionRect() {
    overlayWindow_.ClearSelectionRect();
    temporarySelection_.active = false;
    pendingFenceCreation_.reset();
    desktopIconReadStatus_ = L"Selection canceled.";
    UpdateDiagnosticsTextControl();
}

void AppController::ApplyFenceFromSelectionRect(const RECT& selectionRect) {
    overlayWindow_.ClearSelectionRect();
    temporarySelection_.active = false;

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
    long long createdFenceId = 0;

    int movedCount = 0;
    if (!movedIcons.empty()) {
        movedCount = desktopIconService_.MoveDesktopIcons(
            desktopResolveResult_.listViewWindow,
            desktopResolveResult_.explorerProcessId,
            movedIcons);
    }

    overlayWindow_.SetFenceRect(fenceRect);
    if (!selectedIcons.empty() && !movedIcons.empty()) {
        createdFenceId = SaveFenceSelection(fenceRect, selectedIcons, movedIcons);
    } else if (persistenceReady_) {
        // Keep empty rectangle as a valid fence even when no icons were hit.
        Persistence::FenceRecord fence{};
        fence.name = L"Desktop Group";
        fence.bounds = fenceRect;
        fence.styleJson = L"{\"source\":\"drag-selection\",\"empty\":true}";
        createdFenceId = fenceRepository_.CreateFence(fence);
        if (createdFenceId > 0) {
            (void)fenceRepository_.ReplaceFenceIcons(createdFenceId, {});
        }
    }

    ReloadManagedFences();
    shouldRestoreManagedFences_ = !managedFences_.empty();
    PersistRuntimeRestoreSession(L"create_fence");
    if (createdFenceId > 0) {
        SetActiveFence(createdFenceId);
    } else {
        UpdateOverlayWindow();
        UpdateDiagnosticsTextControl();
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

long long AppController::SaveFenceSelection(
    const RECT& fenceRect,
    const std::vector<Desktop::DesktopIcon>& originalIcons,
    const std::vector<Desktop::DesktopIcon>& movedIcons) {
    if (!persistenceReady_) {
        return 0;
    }

    Persistence::FenceRecord fence{};
    fence.name = L"Desktop Group";
    fence.bounds = fenceRect;
    fence.styleJson = L"{\"source\":\"drag-selection\"}";
    const long long fenceId = fenceRepository_.CreateFence(fence);
    if (fenceId <= 0) {
        Infrastructure::Logger::Get().Error(L"[Selection] failed to persist fence.");
        return 0;
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
        return 0;
    }

    Infrastructure::Logger::Get().Info(
        L"[Selection] fence persisted. id=" + std::to_wstring(fenceId) +
        L"; iconCount=" + std::to_wstring(rows.size()) +
        L"; rect=" + RectToString(fenceRect));
    return fenceId;
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
    text += L"Control Mode: ExplorerDriven\r\n";
    text += L"Persistence: " + std::wstring(persistenceReady_ ? L"ready" : L"not ready") + L"\r\n";
    text += L"Managed Fences: " + std::to_wstring(managedFences_.size()) + L"\r\n";
    text += L"Active Fence: ";
    if (activeFenceId_.has_value()) {
        text += std::to_wstring(*activeFenceId_);
        if (const std::optional<size_t> activeIndex = FindManagedFenceIndexById(*activeFenceId_);
            activeIndex.has_value()) {
            const ManagedFenceState& activeFence = managedFences_[*activeIndex];
            text += L" | ";
            text += activeFence.record.name.empty() ? L"Desktop Group" : activeFence.record.name;
            text += L" | icons=" + std::to_wstring(activeFence.icons.size());
            text += L" | bounds=" + RectToString(activeFence.record.bounds);
        }
    } else {
        text += L"none";
    }
    text += L"\r\n";
    if (persistenceReady_) {
        text += L"Database Path: " + database_.DatabasePath().wstring() + L"\r\n";
    } else if (!database_.LastError().empty()) {
        text += L"Database Error: " + database_.LastError() + L"\r\n";
    }
    if (!managedFences_.empty()) {
        text += L"Fence List:\r\n";
        const size_t sampleFenceCount = std::min<size_t>(managedFences_.size(), 12);
        for (size_t i = 0; i < sampleFenceCount; ++i) {
            const ManagedFenceState& managedFence = managedFences_[i];
            text += L"  [" + std::to_wstring(i) + L"] id=" + std::to_wstring(managedFence.record.id);
            if (activeFenceId_.has_value() && managedFence.record.id == *activeFenceId_) {
                text += L" *active*";
            }
            text += L" name=" + (managedFence.record.name.empty() ? std::wstring(L"Desktop Group") : managedFence.record.name);
            text += L" icons=" + std::to_wstring(managedFence.icons.size());
            text += L" bounds=" + RectToString(managedFence.record.bounds) + L"\r\n";
        }
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
