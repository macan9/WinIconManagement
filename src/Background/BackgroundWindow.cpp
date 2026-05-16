#include "Background/BackgroundWindow.h"

#include <array>
#include <algorithm>
#include <sstream>
#include <string>

#include "Infrastructure/Logger.h"

namespace {
constexpr wchar_t kBackgroundWindowClassName[] = L"WinIconManagement.BackgroundWindow";
constexpr int kFenceCornerRadiusPixels = 10;
constexpr COLORREF kFenceFillColor = RGB(202, 231, 214);
constexpr COLORREF kFenceBorderColor = RGB(62, 132, 95);
constexpr int kFenceBorderWidth = 1;

std::wstring RectToString(const RECT& rect) {
    return L"[" + std::to_wstring(rect.left) + L"," + std::to_wstring(rect.top) +
           L"]-[" + std::to_wstring(rect.right) + L"," + std::to_wstring(rect.bottom) + L"]";
}

RECT ScreenRectToHostClientRect(const RECT& screenRect, HWND hostWindow) {
    POINT topLeft{screenRect.left, screenRect.top};
    POINT bottomRight{screenRect.right, screenRect.bottom};
    if (hostWindow != nullptr && IsWindow(hostWindow)) {
        ScreenToClient(hostWindow, &topLeft);
        ScreenToClient(hostWindow, &bottomRight);
    }

    return RECT{
        topLeft.x,
        topLeft.y,
        bottomRight.x,
        bottomRight.y};
}

bool RectEquals(const RECT& left, const RECT& right) {
    return left.left == right.left &&
           left.top == right.top &&
           left.right == right.right &&
           left.bottom == right.bottom;
}

std::wstring HandleToString(HWND handle) {
    if (handle == nullptr) {
        return L"0x0";
    }

    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(handle);
    return stream.str();
}

std::wstring GetWindowClassName(HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        return L"<null>";
    }

    std::array<wchar_t, 256> buffer{};
    const int length = GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
    if (length <= 0) {
        return L"<unknown>";
    }
    return std::wstring(buffer.data(), static_cast<size_t>(length));
}

std::wstring DesktopHostLayerToString(Background::DesktopHostLayer layer) {
    switch (layer) {
        case Background::DesktopHostLayer::BehindExplorerIcons:
            return L"BehindExplorerIcons";
        case Background::DesktopHostLayer::ShellDefViewBehindListView:
            return L"ShellDefViewBehindListView";
        case Background::DesktopHostLayer::Fallback:
        default:
            return L"Fallback";
    }
}
}

namespace Background {
BackgroundWindow::BackgroundWindow()
    : instance_(nullptr),
      ownerWindow_(nullptr),
      desktopHostWindow_(nullptr),
      desktopHostLayer_(DesktopHostLayer::Fallback),
      virtualDesktopRect_{0, 0, 0, 0},
      fenceRects_(),
      fenceWindows_(),
      visible_(false),
      shouldBeVisible_(false) {}

BackgroundWindow::~BackgroundWindow() {
    Destroy();
}

bool BackgroundWindow::Initialize(HINSTANCE instance, HWND ownerWindow) {
    if (instance_ != nullptr) {
        return true;
    }

    instance_ = instance;
    ownerWindow_ = ownerWindow;
    if (!RegisterClass()) {
        return false;
    }

    Infrastructure::Logger::Get().Info(
        L"[Background] Initialize success (per-fence child windows; creation deferred until host is ready).");
    return true;
}

void BackgroundWindow::Destroy() {
    DestroyFenceWindows();
    visible_ = false;
    shouldBeVisible_ = false;
    desktopHostWindow_ = nullptr;
}

bool BackgroundWindow::IsInitialized() const {
    return instance_ != nullptr;
}

bool BackgroundWindow::IsVisible() const {
    return visible_;
}

HWND BackgroundWindow::Handle() const {
    if (fenceWindows_.empty()) {
        return nullptr;
    }
    return fenceWindows_.front().window;
}

void BackgroundWindow::Show() {
    shouldBeVisible_ = true;
    EnsureHostWindowReady();
    if (fenceWindows_.size() != fenceRects_.size()) {
        RebuildFenceWindows();
    } else {
        UpdateFenceWindowBounds();
    }
    ShowFenceWindows();
    visible_ = !fenceWindows_.empty();
    Infrastructure::Logger::Get().Info(
        L"[Background] Show. windowCount=" + std::to_wstring(fenceWindows_.size()));
}

void BackgroundWindow::Hide() {
    shouldBeVisible_ = false;
    HideFenceWindows();
    visible_ = false;
    Infrastructure::Logger::Get().Info(L"[Background] Hide.");
}

void BackgroundWindow::SetVirtualDesktopRect(const RECT& virtualDesktopRect) {
    virtualDesktopRect_ = virtualDesktopRect;
    UpdateFenceWindowBounds();
}

void BackgroundWindow::SetDesktopHostWindow(HWND desktopHostWindow, DesktopHostLayer desktopHostLayer) {
    if (desktopHostWindow_ == desktopHostWindow && desktopHostLayer_ == desktopHostLayer) {
        if (shouldBeVisible_) {
            EnsureHostWindowReady();
            ShowFenceWindows();
            visible_ = !fenceWindows_.empty();
        }
        return;
    }

    desktopHostWindow_ = desktopHostWindow;
    desktopHostLayer_ = desktopHostLayer;
    if (desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        Infrastructure::Logger::Get().Info(L"[Background] Desktop host unavailable; destroy fence windows.");
        DestroyFenceWindows();
        visible_ = false;
        return;
    }

    Infrastructure::Logger::Get().Info(
        L"[Background] Desktop host changed. newHost=" + HandleToString(desktopHostWindow_) +
        L"; layer=" + DesktopHostLayerToString(desktopHostLayer_) +
        L"; class=" + GetWindowClassName(desktopHostWindow_));
    LogHostDiagnostics(L"host_changed");
    EnsureHostWindowReady();
    if (!fenceRects_.empty()) {
        RebuildFenceWindows();
    }
    if (shouldBeVisible_) {
        ShowFenceWindows();
        visible_ = !fenceWindows_.empty();
    }
}

void BackgroundWindow::SetFenceRects(const std::vector<RECT>& fenceRects) {
    bool changed = fenceRects_.size() != fenceRects.size();
    if (!changed) {
        for (size_t i = 0; i < fenceRects.size(); ++i) {
            if (!RectEquals(fenceRects_[i], fenceRects[i])) {
                changed = true;
                break;
            }
        }
    }

    fenceRects_ = fenceRects;
    if (changed || fenceWindows_.size() != fenceRects_.size()) {
        RebuildFenceWindows();
    } else {
        UpdateFenceWindowBounds();
    }
    if (shouldBeVisible_) {
        ShowFenceWindows();
        visible_ = !fenceWindows_.empty();
    }
}

LRESULT CALLBACK BackgroundWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    BackgroundWindow* background = nullptr;
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        background = reinterpret_cast<BackgroundWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(background));
    } else {
        background = reinterpret_cast<BackgroundWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (background != nullptr) {
        return background->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT BackgroundWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintFenceWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool BackgroundWindow::RegisterClass() const {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &BackgroundWindow::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    windowClass.lpszClassName = kBackgroundWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            Infrastructure::Logger::Get().Error(
                L"[Background] RegisterClassExW failed. error=" + std::to_wstring(error));
            return false;
        }
    }
    return true;
}

HWND BackgroundWindow::CreateFenceWindow(const RECT& fenceRect) {
    if (desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        return nullptr;
    }

    const RECT hostRect = ScreenRectToHostClientRect(fenceRect, desktopHostWindow_);
    const int width = std::max(1L, hostRect.right - hostRect.left);
    const int height = std::max(1L, hostRect.bottom - hostRect.top);
    HWND window = CreateWindowExW(
        0,
        kBackgroundWindowClassName,
        L"WinIconManagement Fence Background",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        hostRect.left,
        hostRect.top,
        width,
        height,
        desktopHostWindow_,
        nullptr,
        instance_,
        this);

    if (window == nullptr) {
        Infrastructure::Logger::Get().Error(
            L"[Background] CreateWindowExW failed for fence rect=" + RectToString(fenceRect) +
            L"; error=" + std::to_wstring(GetLastError()));
        return nullptr;
    }

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, kFenceCornerRadiusPixels * 2, kFenceCornerRadiusPixels * 2);
    if (region != nullptr) {
        SetWindowRgn(window, region, TRUE);
    }

    SetWindowPos(
        window,
        GetFenceWindowZOrderTarget(),
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_SHOWWINDOW);

    LogFenceWindowDiagnostics(window, fenceRect);
    return window;
}

void BackgroundWindow::DestroyFenceWindows() {
    for (const FenceVisualWindow& fenceWindow : fenceWindows_) {
        if (fenceWindow.window != nullptr && IsWindow(fenceWindow.window)) {
            DestroyWindow(fenceWindow.window);
        }
    }
    fenceWindows_.clear();
}

void BackgroundWindow::RebuildFenceWindows() {
    DestroyFenceWindows();
    if (instance_ == nullptr || desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        return;
    }

    EnsureHostWindowReady();
    if (!IsDesktopHostUsable()) {
        Infrastructure::Logger::Get().Error(
            L"[Background] Rebuild skipped: desktop host is not usable for a background child layer.");
        return;
    }
    for (const RECT& fenceRect : fenceRects_) {
        RECT normalized = fenceRect;
        if (normalized.left > normalized.right) {
            std::swap(normalized.left, normalized.right);
        }
        if (normalized.top > normalized.bottom) {
            std::swap(normalized.top, normalized.bottom);
        }
        if ((normalized.right - normalized.left) <= 0 || (normalized.bottom - normalized.top) <= 0) {
            continue;
        }

        HWND window = CreateFenceWindow(normalized);
        if (window == nullptr) {
            continue;
        }

        fenceWindows_.push_back(FenceVisualWindow{
            window,
            normalized});
    }

    EnsureDesktopLayerZOrder();
}

void BackgroundWindow::UpdateFenceWindowBounds() {
    if (desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        return;
    }

    for (FenceVisualWindow& fenceWindow : fenceWindows_) {
        if (fenceWindow.window == nullptr || !IsWindow(fenceWindow.window)) {
            continue;
        }

        const RECT hostRect = ScreenRectToHostClientRect(fenceWindow.screenRect, desktopHostWindow_);
        const int width = std::max(1L, hostRect.right - hostRect.left);
        const int height = std::max(1L, hostRect.bottom - hostRect.top);
        SetWindowPos(
            fenceWindow.window,
            GetFenceWindowZOrderTarget(),
            hostRect.left,
            hostRect.top,
            width,
            height,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_SHOWWINDOW);
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, kFenceCornerRadiusPixels * 2, kFenceCornerRadiusPixels * 2);
        if (region != nullptr) {
            SetWindowRgn(fenceWindow.window, region, TRUE);
        }
        InvalidateRect(fenceWindow.window, nullptr, TRUE);
        UpdateWindow(fenceWindow.window);
    }

    EnsureDesktopLayerZOrder();
}

void BackgroundWindow::ShowFenceWindows() {
    for (const FenceVisualWindow& fenceWindow : fenceWindows_) {
        if (fenceWindow.window == nullptr || !IsWindow(fenceWindow.window)) {
            continue;
        }
        ShowWindow(fenceWindow.window, SW_SHOWNOACTIVATE);
        RedrawWindow(
            fenceWindow.window,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
    }
    EnsureDesktopLayerZOrder();
}

void BackgroundWindow::HideFenceWindows() {
    for (const FenceVisualWindow& fenceWindow : fenceWindows_) {
        if (fenceWindow.window == nullptr || !IsWindow(fenceWindow.window)) {
            continue;
        }
        ShowWindow(fenceWindow.window, SW_HIDE);
    }
}

void BackgroundWindow::EnsureDesktopLayerZOrder() const {
    for (const FenceVisualWindow& fenceWindow : fenceWindows_) {
        if (fenceWindow.window == nullptr || !IsWindow(fenceWindow.window)) {
            continue;
        }
        SetWindowPos(
            fenceWindow.window,
            GetFenceWindowZOrderTarget(),
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
    }
}

void BackgroundWindow::EnsureHostWindowReady() const {
    if (desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        return;
    }

    // Explorer owns this host. Do not ShowWindow/resize/reorder it, or a blank WorkerW can become a full-screen veil.
    UpdateWindow(desktopHostWindow_);
}

bool BackgroundWindow::IsDesktopHostUsable() const {
    if (desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        return false;
    }
    const RECT targetRect = GetEffectiveVirtualDesktopRect();
    const int targetWidth = targetRect.right - targetRect.left;
    const int targetHeight = targetRect.bottom - targetRect.top;
    if (targetWidth <= 0 || targetHeight <= 0) {
        return false;
    }

    RECT hostWindowRect{};
    RECT hostClientRect{};
    GetWindowRect(desktopHostWindow_, &hostWindowRect);
    GetClientRect(desktopHostWindow_, &hostClientRect);
    const int hostWidth = hostWindowRect.right - hostWindowRect.left;
    const int hostHeight = hostWindowRect.bottom - hostWindowRect.top;
    const int clientWidth = hostClientRect.right - hostClientRect.left;
    const int clientHeight = hostClientRect.bottom - hostClientRect.top;
    const bool hostLooksLikeDesktop =
        IsWindowVisible(desktopHostWindow_) != FALSE &&
        hostWindowRect.left <= targetRect.left &&
        hostWindowRect.top <= targetRect.top &&
        hostWidth >= targetWidth &&
        hostHeight >= targetHeight &&
        clientWidth > 0 &&
        clientHeight > 0;

    if (!hostLooksLikeDesktop) {
        Infrastructure::Logger::Get().Info(
            L"[Background] Host rejected. reason=not_desktop_sized_or_not_visible; layer=" +
            DesktopHostLayerToString(desktopHostLayer_) +
            L"; host=" + HandleToString(desktopHostWindow_) +
            L"; visible=" + std::wstring(IsWindowVisible(desktopHostWindow_) ? L"true" : L"false") +
            L"; windowRect=" + RectToString(hostWindowRect) +
            L"; clientRect=" + RectToString(hostClientRect) +
            L"; targetRect=" + RectToString(targetRect));
    }
    return hostLooksLikeDesktop;
}

RECT BackgroundWindow::GetEffectiveVirtualDesktopRect() const {
    if (virtualDesktopRect_.right > virtualDesktopRect_.left &&
        virtualDesktopRect_.bottom > virtualDesktopRect_.top) {
        return virtualDesktopRect_;
    }

    RECT virtualDesktopRect{};
    virtualDesktopRect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    virtualDesktopRect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    virtualDesktopRect.right = virtualDesktopRect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virtualDesktopRect.bottom = virtualDesktopRect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return virtualDesktopRect;
}

HWND BackgroundWindow::GetFenceWindowZOrderTarget() const {
    if (desktopHostLayer_ == DesktopHostLayer::BehindExplorerIcons) {
        return HWND_TOP;
    }
    return HWND_BOTTOM;
}

void BackgroundWindow::LogHostDiagnostics(const wchar_t* reason) const {
    if (desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        return;
    }

    RECT hostWindowRect{};
    RECT hostClientRect{};
    GetWindowRect(desktopHostWindow_, &hostWindowRect);
    GetClientRect(desktopHostWindow_, &hostClientRect);

    std::wstring children;
    HWND child = nullptr;
    int childCount = 0;
    while ((child = FindWindowExW(desktopHostWindow_, child, nullptr, nullptr)) != nullptr) {
        ++childCount;
        if (childCount <= 12) {
            RECT childRect{};
            GetWindowRect(child, &childRect);
            children += L" {";
            children += std::to_wstring(childCount);
            children += L":";
            children += HandleToString(child);
            children += L",";
            children += GetWindowClassName(child);
            children += L",visible=";
            children += IsWindowVisible(child) ? L"true" : L"false";
            children += L",rect=";
            children += RectToString(childRect);
            children += L"}";
        }
    }

    Infrastructure::Logger::Get().Info(
        L"[Background] HostDiagnostics reason=" + std::wstring(reason != nullptr ? reason : L"<null>") +
        L"; host=" + HandleToString(desktopHostWindow_) +
        L"; class=" + GetWindowClassName(desktopHostWindow_) +
        L"; layer=" + DesktopHostLayerToString(desktopHostLayer_) +
        L"; hostVisible=" + std::wstring(IsWindowVisible(desktopHostWindow_) ? L"true" : L"false") +
        L"; windowRect=" + RectToString(hostWindowRect) +
        L"; clientRect=" + RectToString(hostClientRect) +
        L"; childCount=" + std::to_wstring(childCount) +
        L"; children=" + children);
}

void BackgroundWindow::LogFenceWindowDiagnostics(HWND fenceWindow, const RECT& fenceRect) const {
    if (fenceWindow == nullptr || !IsWindow(fenceWindow)) {
        return;
    }

    RECT windowRect{};
    RECT clientRect{};
    GetWindowRect(fenceWindow, &windowRect);
    GetClientRect(fenceWindow, &clientRect);
    const HWND previousSibling = GetWindow(fenceWindow, GW_HWNDPREV);
    const HWND nextSibling = GetWindow(fenceWindow, GW_HWNDNEXT);

    Infrastructure::Logger::Get().Info(
        L"[Background] Fence window created. hwnd=" + HandleToString(fenceWindow) +
        L"; rect=" + RectToString(fenceRect) +
        L"; host=" + HandleToString(desktopHostWindow_) +
        L"; hostClass=" + GetWindowClassName(desktopHostWindow_) +
        L"; layer=" + DesktopHostLayerToString(desktopHostLayer_) +
        L"; zTarget=" + (GetFenceWindowZOrderTarget() == HWND_TOP ? std::wstring(L"HWND_TOP") : std::wstring(L"HWND_BOTTOM")) +
        L"; previousSibling=" + HandleToString(previousSibling) +
        L"; previousSiblingClass=" + GetWindowClassName(previousSibling) +
        L"; nextSibling=" + HandleToString(nextSibling) +
        L"; nextSiblingClass=" + GetWindowClassName(nextSibling) +
        L"; windowRect=" + RectToString(windowRect) +
        L"; clientRect=" + RectToString(clientRect) +
        L"; visible=" + std::wstring(IsWindowVisible(fenceWindow) ? L"true" : L"false") +
        L"; parentVisible=" + std::wstring(IsWindowVisible(desktopHostWindow_) ? L"true" : L"false"));
}

void BackgroundWindow::PaintFenceWindow(HWND hwnd) const {
    const FenceVisualWindow* fenceWindow = FindFenceWindow(hwnd);
    if (fenceWindow == nullptr) {
        return;
    }

    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

    HBRUSH fillBrush = CreateSolidBrush(kFenceFillColor);
    FillRect(hdc, &clientRect, fillBrush);
    DeleteObject(fillBrush);

    HPEN borderPen = CreatePen(PS_SOLID, kFenceBorderWidth, kFenceBorderColor);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(
        hdc,
        clientRect.left,
        clientRect.top,
        clientRect.right,
        clientRect.bottom,
        kFenceCornerRadiusPixels * 2,
        kFenceCornerRadiusPixels * 2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    EndPaint(hwnd, &paint);

    Infrastructure::Logger::Get().Info(
        L"[Background] Fence window painted. hwnd=0x" +
        std::to_wstring(reinterpret_cast<uintptr_t>(hwnd)) +
        L"; clientRect=" + RectToString(clientRect));
}

const BackgroundWindow::FenceVisualWindow* BackgroundWindow::FindFenceWindow(HWND hwnd) const {
    for (const FenceVisualWindow& fenceWindow : fenceWindows_) {
        if (fenceWindow.window == hwnd) {
            return &fenceWindow;
        }
    }
    return nullptr;
}
}  // namespace Background
