#include "Background/BackgroundWindow.h"

#include <algorithm>
#include <string>

#include "Infrastructure/Logger.h"

namespace {
constexpr wchar_t kBackgroundWindowClassName[] = L"WinIconManagement.BackgroundWindow";
constexpr int kFenceCornerRadiusPixels = 10;
constexpr COLORREF kFenceFillColor = RGB(214, 228, 255);
constexpr COLORREF kFenceBorderColor = RGB(120, 157, 235);
constexpr int kFenceBorderWidth = 2;

RECT FenceRectToLocalRect(const RECT& screenRect, const RECT& virtualDesktopRect) {
    return RECT{
        screenRect.left - virtualDesktopRect.left,
        screenRect.top - virtualDesktopRect.top,
        screenRect.right - virtualDesktopRect.left,
        screenRect.bottom - virtualDesktopRect.top};
}

RECT ClampRectToClient(const RECT& rect, const RECT& clientRect) {
    RECT clamped = rect;
    clamped.left = std::max(clamped.left, clientRect.left);
    clamped.top = std::max(clamped.top, clientRect.top);
    clamped.right = std::min(clamped.right, clientRect.right);
    clamped.bottom = std::min(clamped.bottom, clientRect.bottom);
    return clamped;
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

void ForceImmediateRedraw(HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        return;
    }

    RedrawWindow(
        window,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}
}

namespace Background {
BackgroundWindow::BackgroundWindow()
    : instance_(nullptr),
      ownerWindow_(nullptr),
      window_(nullptr),
      desktopHostWindow_(nullptr),
      virtualDesktopRect_{0, 0, 0, 0},
      fenceRects_(),
      visible_(false),
      shouldBeVisible_(false),
      paintLogged_(false) {}

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

    Infrastructure::Logger::Get().Info(L"[Background] Initialize success (window creation deferred until host is ready).");
    return true;
}

void BackgroundWindow::Destroy() {
    if (window_ != nullptr && IsWindow(window_)) {
        Infrastructure::Logger::Get().Info(L"[Background] Destroy window.");
        DestroyWindow(window_);
        window_ = nullptr;
    }
    visible_ = false;
    shouldBeVisible_ = false;
    paintLogged_ = false;
}

bool BackgroundWindow::IsInitialized() const {
    return window_ != nullptr && IsWindow(window_);
}

HWND BackgroundWindow::Handle() const {
    return window_;
}

void BackgroundWindow::Show() {
    shouldBeVisible_ = true;
    if (!EnsureWindowCreated()) {
        return;
    }

    EnsureDesktopLayerZOrder();
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    ForceImmediateRedraw(window_);
    visible_ = true;
    Infrastructure::Logger::Get().Info(L"[Background] Show.");
}

void BackgroundWindow::Hide() {
    shouldBeVisible_ = false;
    if (!IsInitialized()) {
        return;
    }

    ShowWindow(window_, SW_HIDE);
    visible_ = false;
    Infrastructure::Logger::Get().Info(L"[Background] Hide.");
}

void BackgroundWindow::SetVirtualDesktopRect(const RECT& virtualDesktopRect) {
    virtualDesktopRect_ = virtualDesktopRect;
    if (!EnsureWindowCreated()) {
        return;
    }

    UpdateWindowBounds();
    ApplyFenceRegion();
    EnsureDesktopLayerZOrder();
    InvalidateRect(window_, nullptr, FALSE);
    ForceImmediateRedraw(window_);
}

void BackgroundWindow::SetDesktopHostWindow(HWND desktopHostWindow) {
    if (desktopHostWindow_ == desktopHostWindow) {
        if (shouldBeVisible_ && EnsureWindowCreated()) {
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            ForceImmediateRedraw(window_);
            visible_ = true;
        }
        return;
    }

    desktopHostWindow_ = desktopHostWindow;
    if (desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        Infrastructure::Logger::Get().Info(L"[Background] Desktop host unavailable; skip reparent.");
        if (IsInitialized()) {
            ShowWindow(window_, SW_HIDE);
            visible_ = false;
        }
        return;
    }

    if (!EnsureWindowCreated()) {
        return;
    }

    if (GetParent(window_) != desktopHostWindow_) {
        SetParent(window_, desktopHostWindow_);
        Infrastructure::Logger::Get().Info(
            L"[Background] Reparented to desktop host. hwnd=0x" +
            std::to_wstring(reinterpret_cast<uintptr_t>(desktopHostWindow_)));
    }

    UpdateWindowBounds();
    ApplyFenceRegion();
    EnsureDesktopLayerZOrder();
    if (shouldBeVisible_) {
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        ForceImmediateRedraw(window_);
        visible_ = true;
    }
}

void BackgroundWindow::SetFenceRects(const std::vector<RECT>& fenceRects) {
    fenceRects_ = fenceRects;
    if (!EnsureWindowCreated()) {
        return;
    }

    ApplyFenceRegion();
    InvalidateRect(window_, nullptr, FALSE);
    ForceImmediateRedraw(window_);
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
        case WM_SHOWWINDOW:
            Infrastructure::Logger::Get().Info(
                L"[Background] WM_SHOWWINDOW shown=" + std::to_wstring(wParam != FALSE ? 1 : 0));
            return DefWindowProcW(hwnd, message, wParam, lParam);
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint(hwnd);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (window_ == hwnd) {
                window_ = nullptr;
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool BackgroundWindow::RegisterClass() const {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = 0;
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

bool BackgroundWindow::CreateBackgroundWindow() {
    if (desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        return false;
    }
    if (virtualDesktopRect_.left == virtualDesktopRect_.right ||
        virtualDesktopRect_.top == virtualDesktopRect_.bottom) {
        virtualDesktopRect_.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        virtualDesktopRect_.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        virtualDesktopRect_.right = virtualDesktopRect_.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        virtualDesktopRect_.bottom = virtualDesktopRect_.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

    const RECT hostClientRect = ScreenRectToHostClientRect(virtualDesktopRect_, desktopHostWindow_);
    const int width = std::max(1, static_cast<int>(hostClientRect.right - hostClientRect.left));
    const int height = std::max(1, static_cast<int>(hostClientRect.bottom - hostClientRect.top));
    window_ = CreateWindowExW(
        0,
        kBackgroundWindowClassName,
        L"WinIconManagement Background",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        hostClientRect.left,
        hostClientRect.top,
        width,
        height,
        desktopHostWindow_,
        nullptr,
        instance_,
        this);

    if (window_ == nullptr) {
        Infrastructure::Logger::Get().Error(
            L"[Background] CreateWindowExW failed. error=" + std::to_wstring(GetLastError()));
        return false;
    }

    ApplyFenceRegion();
    EnsureDesktopLayerZOrder();
    if (shouldBeVisible_) {
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        ForceImmediateRedraw(window_);
        visible_ = true;
    }
    Infrastructure::Logger::Get().Info(
        L"[Background] CreateWindowExW success. hwnd=0x" +
        std::to_wstring(reinterpret_cast<uintptr_t>(window_)) +
        L"; width=" + std::to_wstring(width) +
        L"; height=" + std::to_wstring(height) +
        L"; host=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(desktopHostWindow_)) +
        L"; visible=" + std::wstring(shouldBeVisible_ ? L"true" : L"false"));
    return true;
}

bool BackgroundWindow::EnsureWindowCreated() {
    if (IsInitialized()) {
        return true;
    }

    if (instance_ == nullptr || desktopHostWindow_ == nullptr || !IsWindow(desktopHostWindow_)) {
        return false;
    }

    return CreateBackgroundWindow();
}

void BackgroundWindow::EnsureDesktopLayerZOrder() const {
    if (!IsInitialized()) {
        return;
    }

    SetWindowPos(
        window_,
        HWND_BOTTOM,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
}

void BackgroundWindow::UpdateWindowBounds() {
    if (!IsInitialized()) {
        return;
    }

    const RECT hostClientRect = ScreenRectToHostClientRect(virtualDesktopRect_, desktopHostWindow_);
    SetWindowPos(
        window_,
        HWND_BOTTOM,
        hostClientRect.left,
        hostClientRect.top,
        std::max(1L, hostClientRect.right - hostClientRect.left),
        std::max(1L, hostClientRect.bottom - hostClientRect.top),
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | (shouldBeVisible_ ? SWP_SHOWWINDOW : 0));
}

void BackgroundWindow::ApplyFenceRegion() {
    if (!IsInitialized()) {
        return;
    }

    HRGN combinedRegion = CreateRectRgn(0, 0, 0, 0);
    if (combinedRegion == nullptr) {
        return;
    }

    for (const RECT& fenceRect : fenceRects_) {
        RECT localRect = FenceRectToLocalRect(fenceRect, virtualDesktopRect_);
        if (IsRectEmpty(&localRect)) {
            continue;
        }

        HRGN fenceRegion = CreateRoundRectRgn(
            localRect.left,
            localRect.top,
            localRect.right + 1,
            localRect.bottom + 1,
            kFenceCornerRadiusPixels * 2,
            kFenceCornerRadiusPixels * 2);
        if (fenceRegion == nullptr) {
            continue;
        }

        CombineRgn(combinedRegion, combinedRegion, fenceRegion, RGN_OR);
        DeleteObject(fenceRegion);
    }

    SetWindowRgn(window_, combinedRegion, TRUE);
}

void BackgroundWindow::Paint(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    RECT paintRect = paint.rcPaint;
    if (IsRectEmpty(&paintRect)) {
        paintRect = clientRect;
    }

    HBRUSH fenceFillBrush = CreateSolidBrush(kFenceFillColor);
    FillRect(hdc, &paintRect, fenceFillBrush);
    DeleteObject(fenceFillBrush);

    if (!fenceRects_.empty()) {
        HPEN fenceBorderPen = CreatePen(PS_SOLID, kFenceBorderWidth, kFenceBorderColor);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        HGDIOBJ oldPen = SelectObject(hdc, fenceBorderPen);

        for (const RECT& fenceRect : fenceRects_) {
            RECT localRect = FenceRectToLocalRect(fenceRect, virtualDesktopRect_);
            localRect = ClampRectToClient(localRect, clientRect);
            if (IsRectEmpty(&localRect)) {
                continue;
            }

            RoundRect(
                hdc,
                localRect.left,
                localRect.top,
                localRect.right,
                localRect.bottom,
                kFenceCornerRadiusPixels * 2,
                kFenceCornerRadiusPixels * 2);
        }

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(fenceBorderPen);
    }

    EndPaint(hwnd, &paint);

    if (!paintLogged_) {
        paintLogged_ = true;
        Infrastructure::Logger::Get().Info(
            L"[Background] WM_PAINT first frame. fenceCount=" + std::to_wstring(fenceRects_.size()));
    }
}
}  // namespace Background
