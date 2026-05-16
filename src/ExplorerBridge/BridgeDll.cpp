#include <windows.h>

#include <commctrl.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kBridgeWindowClassName[] = L"WinIconManagement.ExplorerBridge.MessageWindow";
constexpr UINT_PTR kRetryTimerId = 1;
constexpr UINT_PTR kPaintBurstTimerId = 2;
constexpr UINT kRetryIntervalMs = 1000;
constexpr UINT kPaintBurstIntervalMs = 80;
constexpr int kMaxResolveAttempts = 30;
constexpr int kPaintBurstMaxTicks = 6;
constexpr ULONG_PTR kFenceRectsCopyDataId = 0x57494D46;  // WIMF

struct FenceRectsPayloadHeader {
    UINT32 version;
    UINT32 count;
};

HMODULE g_module = nullptr;
HWND g_messageWindow = nullptr;
HWND g_defView = nullptr;
HWND g_listView = nullptr;
WNDPROC g_originalDefViewProc = nullptr;
WNDPROC g_originalListViewProc = nullptr;
std::vector<RECT> g_fenceRects;
std::vector<RECT> g_previousFenceRects;
int g_resolveAttempts = 0;
int g_paintBurstTicksRemaining = 0;
int g_paintLogCount = 0;
int g_redrawObserveLogCount = 0;
DWORD g_lastNativeRedrawPaintTick = 0;
std::wofstream g_log;

std::wstring Timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond);
    return buffer;
}

void Log(const std::wstring& message) {
    if (!g_log.is_open()) {
        wchar_t tempPath[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, tempPath) == 0) {
            return;
        }
        const std::wstring path = std::wstring(tempPath) + L"WinIconManagement.ExplorerBridge.log";
        g_log.open(path, std::ios::out | std::ios::app);
    }

    if (g_log.is_open()) {
        g_log << L"[" << Timestamp() << L"] " << message << L"\n";
        g_log.flush();
    }
}

std::wstring HandleToString(HWND hwnd) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(hwnd);
    return stream.str();
}

std::wstring ClassNameOf(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return L"<null>";
    }

    std::array<wchar_t, 256> buffer{};
    const int length = GetClassNameW(hwnd, buffer.data(), static_cast<int>(buffer.size()));
    if (length <= 0) {
        return L"<unknown>";
    }
    return std::wstring(buffer.data(), static_cast<size_t>(length));
}

int RectWidth(const RECT& rect) {
    const int width = static_cast<int>(rect.right - rect.left);
    return width > 1 ? width : 1;
}

int RectHeight(const RECT& rect) {
    const int height = static_cast<int>(rect.bottom - rect.top);
    return height > 1 ? height : 1;
}

RECT NormalizeRect(RECT rect) {
    if (rect.left > rect.right) {
        const LONG value = rect.left;
        rect.left = rect.right;
        rect.right = value;
    }
    if (rect.top > rect.bottom) {
        const LONG value = rect.top;
        rect.top = rect.bottom;
        rect.bottom = value;
    }
    return rect;
}

RECT InflateCopy(RECT rect, int amount) {
    InflateRect(&rect, amount, amount);
    return rect;
}

bool UnionRects(const std::vector<RECT>& rects, RECT* result) {
    if (result == nullptr || rects.empty()) {
        return false;
    }

    RECT combined = rects.front();
    for (size_t index = 1; index < rects.size(); ++index) {
        UnionRect(&combined, &combined, &rects[index]);
    }
    *result = combined;
    return true;
}

struct FindDesktopContext {
    HWND shellDefView = nullptr;
    HWND listView = nullptr;
};

BOOL CALLBACK EnumWindowsForDesktop(HWND topLevelWindow, LPARAM parameter) {
    auto* context = reinterpret_cast<FindDesktopContext*>(parameter);
    HWND defView = FindWindowExW(topLevelWindow, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defView == nullptr) {
        return TRUE;
    }

    HWND listView = FindWindowExW(defView, nullptr, L"SysListView32", L"FolderView");
    if (listView == nullptr) {
        listView = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
    }

    if (listView != nullptr) {
        context->shellDefView = defView;
        context->listView = listView;
        return FALSE;
    }
    return TRUE;
}

HWND FindDesktopListView() {
    FindDesktopContext context{};
    EnumWindows(&EnumWindowsForDesktop, reinterpret_cast<LPARAM>(&context));
    if (context.listView != nullptr) {
        g_defView = context.shellDefView;
        Log(
            L"Resolved desktop ListView=" + HandleToString(context.listView) +
            L"; class=" + ClassNameOf(context.listView) +
            L"; defView=" + HandleToString(context.shellDefView));
    }
    return context.listView;
}

void ExcludeListViewIconBounds(HWND listView, HDC hdc) {
    const int itemCount = static_cast<int>(SendMessageW(listView, LVM_GETITEMCOUNT, 0, 0));
    for (int index = 0; index < itemCount; ++index) {
        RECT itemRect{};
        itemRect.left = LVIR_BOUNDS;
        if (SendMessageW(listView, LVM_GETITEMRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&itemRect)) == FALSE) {
            continue;
        }

        InflateRect(&itemRect, 8, 8);
        ExcludeClipRect(hdc, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom);
    }
}

void DrawFenceRect(HWND listView, HDC hdc, const RECT& screenFenceRect) {
    RECT listViewScreen{};
    GetWindowRect(listView, &listViewScreen);

    RECT localFence{
        screenFenceRect.left - listViewScreen.left,
        screenFenceRect.top - listViewScreen.top,
        screenFenceRect.right - listViewScreen.left,
        screenFenceRect.bottom - listViewScreen.top};
    localFence = NormalizeRect(localFence);

    const int savedDc = SaveDC(hdc);
    HRGN fenceRegion = CreateRoundRectRgn(
        localFence.left,
        localFence.top,
        localFence.right,
        localFence.bottom,
        28,
        28);
    if (fenceRegion != nullptr) {
        SelectClipRgn(hdc, fenceRegion);
        DeleteObject(fenceRegion);
    }

    HBRUSH fillBrush = CreateSolidBrush(RGB(166, 221, 189));
    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(45, 122, 78));
    HGDIOBJ oldBrush = SelectObject(hdc, fillBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    RoundRect(
        hdc,
        localFence.left,
        localFence.top,
        localFence.right,
        localFence.bottom,
        28,
        28);

    RECT titleBand{
        localFence.left + 2,
        localFence.top + 2,
        localFence.right - 2,
        localFence.top + 34};
    HBRUSH titleBrush = CreateSolidBrush(RGB(120, 194, 151));
    FillRect(hdc, &titleBand, titleBrush);
    DeleteObject(titleBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(18, 67, 42));
    RECT textRect{localFence.left + 12, localFence.top + 8, localFence.right - 12, localFence.top + 32};
    DrawTextW(hdc, L"Desktop Group", -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);
    RestoreDC(hdc, savedDc);
}

void PaintFenceRects() {
    if (g_listView == nullptr || !IsWindow(g_listView) || g_fenceRects.empty()) {
        return;
    }

    HDC hdc = GetDC(g_listView);
    if (hdc == nullptr) {
        return;
    }

    for (const RECT& fenceRect : g_fenceRects) {
        DrawFenceRect(g_listView, hdc, fenceRect);
    }
    ReleaseDC(g_listView, hdc);

    if (g_paintLogCount < 20) {
        ++g_paintLogCount;
        Log(L"Painted fence rect burst frame. count=" + std::to_wstring(g_paintLogCount) +
            L"; fenceCount=" + std::to_wstring(g_fenceRects.size()));
    }
}

void InvalidateFenceRects(const std::vector<RECT>& rects) {
    if (g_listView == nullptr || !IsWindow(g_listView) || rects.empty()) {
        return;
    }

    RECT listViewScreen{};
    GetWindowRect(g_listView, &listViewScreen);

    for (const RECT& screenRect : rects) {
        RECT localRect{
            screenRect.left - listViewScreen.left,
            screenRect.top - listViewScreen.top,
            screenRect.right - listViewScreen.left,
            screenRect.bottom - listViewScreen.top};
        localRect = InflateCopy(NormalizeRect(localRect), 12);
        RedrawWindow(g_listView, &localRect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
}

void StartPaintBurst() {
    if (g_messageWindow == nullptr || g_fenceRects.empty()) {
        return;
    }

    g_paintBurstTicksRemaining = kPaintBurstMaxTicks;
    SetTimer(g_messageWindow, kPaintBurstTimerId, kPaintBurstIntervalMs, nullptr);
    PaintFenceRects();
}

void SchedulePaintAfterNativeRedraw() {
    if (g_messageWindow == nullptr || g_fenceRects.empty()) {
        return;
    }

    const DWORD now = GetTickCount();
    if (now - g_lastNativeRedrawPaintTick < 250) {
        return;
    }
    g_lastNativeRedrawPaintTick = now;

    if (g_paintBurstTicksRemaining <= 0) {
        g_paintBurstTicksRemaining = 1;
        SetTimer(g_messageWindow, kPaintBurstTimerId, kPaintBurstIntervalMs, nullptr);
    }
}

void UpdateFenceRectsFromPayload(const COPYDATASTRUCT* copyData) {
    if (copyData == nullptr || copyData->dwData != kFenceRectsCopyDataId ||
        copyData->cbData < sizeof(FenceRectsPayloadHeader) || copyData->lpData == nullptr) {
        return;
    }

    const auto* header = static_cast<const FenceRectsPayloadHeader*>(copyData->lpData);
    if (header->version != 1) {
        Log(L"Ignored fence rect payload with unsupported version=" + std::to_wstring(header->version));
        return;
    }

    const size_t expectedBytes = sizeof(FenceRectsPayloadHeader) + sizeof(RECT) * header->count;
    if (copyData->cbData < expectedBytes) {
        Log(L"Ignored truncated fence rect payload.");
        return;
    }

    g_previousFenceRects = g_fenceRects;
    g_fenceRects.clear();
    const auto* rects = reinterpret_cast<const RECT*>(
        static_cast<const BYTE*>(copyData->lpData) + sizeof(FenceRectsPayloadHeader));
    for (UINT32 index = 0; index < header->count; ++index) {
        RECT rect = NormalizeRect(rects[index]);
        if (RectWidth(rect) >= 20 && RectHeight(rect) >= 20) {
            g_fenceRects.push_back(rect);
        }
    }

    InvalidateFenceRects(g_previousFenceRects);
    InvalidateFenceRects(g_fenceRects);
    StartPaintBurst();
    Log(L"Updated fence rects from host. count=" + std::to_wstring(g_fenceRects.size()));
}

LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WNDPROC original = g_originalListViewProc;
    if (message == WM_NCDESTROY) {
        if (original != nullptr) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
        }
        g_originalListViewProc = nullptr;
        g_listView = nullptr;
        return original != nullptr
            ? CallWindowProcW(original, hwnd, message, wParam, lParam)
            : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    const LRESULT result = original != nullptr
        ? CallWindowProcW(original, hwnd, message, wParam, lParam)
        : DefWindowProcW(hwnd, message, wParam, lParam);

    if (message == WM_PAINT || message == WM_ERASEBKGND || message == WM_SIZE || message == WM_WINDOWPOSCHANGED) {
        if (g_redrawObserveLogCount < 10) {
            ++g_redrawObserveLogCount;
            Log(L"Observed native ListView redraw event; scheduling non-intrusive fence repaint. message=" +
                std::to_wstring(message));
        }
        SchedulePaintAfterNativeRedraw();
    }
    return result;
}

LRESULT CALLBACK DefViewSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WNDPROC original = g_originalDefViewProc;
    if (message == WM_NCDESTROY) {
        if (original != nullptr) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
        }
        g_originalDefViewProc = nullptr;
        g_defView = nullptr;
        return original != nullptr
            ? CallWindowProcW(original, hwnd, message, wParam, lParam)
            : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    const LRESULT result = original != nullptr
        ? CallWindowProcW(original, hwnd, message, wParam, lParam)
        : DefWindowProcW(hwnd, message, wParam, lParam);

    if (message == WM_PAINT || message == WM_ERASEBKGND || message == WM_SIZE || message == WM_WINDOWPOSCHANGED) {
        SchedulePaintAfterNativeRedraw();
    }
    return result;
}

bool AttachToDefView(HWND defView) {
    if (defView == nullptr || !IsWindow(defView)) {
        Log(L"AttachToDefView skipped: DefView unavailable.");
        return false;
    }
    if (g_defView == defView && g_originalDefViewProc != nullptr) {
        return true;
    }

    SetLastError(0);
    LONG_PTR previous = SetWindowLongPtrW(defView, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DefViewSubclassProc));
    if (previous == 0 && GetLastError() != 0) {
        Log(L"SetWindowLongPtrW(GWLP_WNDPROC) for DefView failed. error=" + std::to_wstring(GetLastError()));
        return false;
    }

    g_defView = defView;
    g_originalDefViewProc = reinterpret_cast<WNDPROC>(previous);
    Log(L"Subclass attached to DefView=" + HandleToString(defView));
    return true;
}

bool AttachToListView(HWND listView) {
    if (listView == nullptr || !IsWindow(listView)) {
        return false;
    }
    if (g_listView == listView && g_originalListViewProc != nullptr) {
        return true;
    }

    SetLastError(0);
    LONG_PTR previous = SetWindowLongPtrW(listView, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ListViewSubclassProc));
    if (previous == 0 && GetLastError() != 0) {
        Log(L"SetWindowLongPtrW(GWLP_WNDPROC) for ListView failed. error=" + std::to_wstring(GetLastError()));
        return false;
    }

    g_listView = listView;
    g_originalListViewProc = reinterpret_cast<WNDPROC>(previous);
    Log(L"Subclass attached to ListView=" + HandleToString(listView));
    return true;
}

void TryAttach() {
    if (g_originalListViewProc != nullptr && g_originalDefViewProc != nullptr &&
        g_listView != nullptr && IsWindow(g_listView) &&
        g_defView != nullptr && IsWindow(g_defView)) {
        if (g_messageWindow != nullptr) {
            KillTimer(g_messageWindow, kRetryTimerId);
        }
        return;
    }

    ++g_resolveAttempts;
    HWND listView = FindDesktopListView();
    const bool listViewAttached = AttachToListView(listView);
    const bool defViewAttached = AttachToDefView(g_defView);
    if (listViewAttached && defViewAttached) {
        if (g_messageWindow != nullptr) {
            KillTimer(g_messageWindow, kRetryTimerId);
        }
        Log(L"ExplorerBridge attached: non-intrusive GetDC repaint service active.");
        return;
    }

    if (g_resolveAttempts >= kMaxResolveAttempts && g_messageWindow != nullptr) {
        Log(L"Attach attempts exhausted.");
        KillTimer(g_messageWindow, kRetryTimerId);
    }
}

void Detach() {
    KillTimer(g_messageWindow, kPaintBurstTimerId);
    InvalidateFenceRects(g_fenceRects);
    if (g_defView != nullptr && IsWindow(g_defView) && g_originalDefViewProc != nullptr) {
        SetWindowLongPtrW(g_defView, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalDefViewProc));
        Log(L"Subclass detached from DefView=" + HandleToString(g_defView));
    }
    if (g_listView != nullptr && IsWindow(g_listView) && g_originalListViewProc != nullptr) {
        SetWindowLongPtrW(g_listView, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalListViewProc));
        RedrawWindow(g_listView, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        Log(L"Subclass detached from ListView=" + HandleToString(g_listView));
    }
    g_originalDefViewProc = nullptr;
    g_originalListViewProc = nullptr;
    g_defView = nullptr;
    g_listView = nullptr;
}

LRESULT CALLBACK BridgeWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g_messageWindow = hwnd;
            SetTimer(hwnd, kRetryTimerId, kRetryIntervalMs, nullptr);
            TryAttach();
            return 0;
        case WM_COPYDATA:
            UpdateFenceRectsFromPayload(reinterpret_cast<const COPYDATASTRUCT*>(lParam));
            return TRUE;
        case WM_TIMER:
            if (wParam == kRetryTimerId) {
                TryAttach();
                return 0;
            }
            if (wParam == kPaintBurstTimerId) {
                if (g_paintBurstTicksRemaining <= 0) {
                    KillTimer(hwnd, kPaintBurstTimerId);
                    return 0;
                }
                --g_paintBurstTicksRemaining;
                PaintFenceRects();
                if (g_paintBurstTicksRemaining <= 0) {
                    KillTimer(hwnd, kPaintBurstTimerId);
                }
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(hwnd, kRetryTimerId);
            KillTimer(hwnd, kPaintBurstTimerId);
            Detach();
            g_messageWindow = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

DWORD WINAPI BridgeThreadProc(void*) {
    Log(L"ExplorerBridge thread started. pid=" + std::to_wstring(GetCurrentProcessId()));

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &BridgeWindowProc;
    windowClass.hInstance = g_module;
    windowClass.lpszClassName = kBridgeWindowClassName;
    RegisterClassExW(&windowClass);

    g_messageWindow = CreateWindowExW(
        0,
        kBridgeWindowClassName,
        L"WinIconManagement ExplorerBridge",
        0,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        g_module,
        nullptr);
    if (g_messageWindow == nullptr) {
        Log(L"Create message window failed. error=" + std::to_wstring(GetLastError()));
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    Detach();
    Log(L"ExplorerBridge thread exiting.");
    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, &BridgeThreadProc, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        Detach();
        if (g_log.is_open()) {
            g_log.close();
        }
    }
    return TRUE;
}
