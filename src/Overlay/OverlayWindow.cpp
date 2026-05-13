#include "Overlay/OverlayWindow.h"

#include <algorithm>
#include <string>
#include <sstream>

#include "Infrastructure/Logger.h"

namespace {
constexpr wchar_t kOverlayWindowClassName[] = L"WinIconManagement.OverlayWindow";
constexpr int kFenceCornerRadiusPixels = 10;
constexpr COLORREF kFenceFillColor = RGB(76, 143, 255);
constexpr COLORREF kFenceBorderColor = RGB(36, 99, 235);
constexpr BYTE kFenceFillAlpha = 70;
constexpr int kFenceBorderWidth = 2;
}

namespace Overlay {
OverlayWindow::OverlayWindow()
    : instance_(nullptr),
      ownerWindow_(nullptr),
      window_(nullptr),
      desktopHostWindow_(nullptr),
      virtualDesktopRect_{0, 0, 0, 0},
      fenceRect_{120, 120, 560, 360},
      fixedMode_(true),
      visible_(false),
      paintLogged_(false) {}

OverlayWindow::~OverlayWindow() {
    Destroy();
}

bool OverlayWindow::Initialize(HINSTANCE instance, HWND ownerWindow) {
    if (window_ != nullptr) {
        return true;
    }

    instance_ = instance;
    ownerWindow_ = ownerWindow;
    if (!RegisterClass()) {
        return false;
    }
    if (!CreateOverlayWindow()) {
        return false;
    }

    ApplyClickThroughStyle();
    ApplyRoundedRegion();
    Infrastructure::Logger::Get().Info(L"[Overlay] Initialize success.");
    return true;
}

void OverlayWindow::Destroy() {
    if (window_ != nullptr && IsWindow(window_)) {
        Infrastructure::Logger::Get().Info(L"[Overlay] Destroy window.");
        DestroyWindow(window_);
        window_ = nullptr;
    }
    visible_ = false;
    paintLogged_ = false;
}

bool OverlayWindow::IsInitialized() const {
    return window_ != nullptr && IsWindow(window_);
}

HWND OverlayWindow::Handle() const {
    return window_;
}

void OverlayWindow::Show() {
    if (!IsInitialized()) {
        return;
    }
    EnsureDesktopLayerZOrder();
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    UpdateWindow(window_);
    visible_ = true;
    Infrastructure::Logger::Get().Info(L"[Overlay] Show.");
}

void OverlayWindow::Hide() {
    if (!IsInitialized()) {
        return;
    }
    ShowWindow(window_, SW_HIDE);
    visible_ = false;
    Infrastructure::Logger::Get().Info(L"[Overlay] Hide.");
}

void OverlayWindow::SetFixedMode(bool fixedMode) {
    fixedMode_ = fixedMode;
    if (IsInitialized()) {
        ApplyClickThroughStyle();
        InvalidateRect(window_, nullptr, TRUE);
    }
    Infrastructure::Logger::Get().Info(
        L"[Overlay] SetFixedMode: " + std::wstring(fixedMode_ ? L"fixed" : L"edit"));
}

void OverlayWindow::SetFenceRect(const RECT& fenceRect) {
    fenceRect_ = fenceRect;
    NormalizeFenceRect();
    if (IsInitialized()) {
        ApplyRoundedRegion();
        InvalidateRect(window_, nullptr, TRUE);
    }
    Infrastructure::Logger::Get().Info(
        L"[Overlay] SetFenceRect: left=" + std::to_wstring(fenceRect_.left) +
        L", top=" + std::to_wstring(fenceRect_.top) +
        L", right=" + std::to_wstring(fenceRect_.right) +
        L", bottom=" + std::to_wstring(fenceRect_.bottom));
}

void OverlayWindow::SetVirtualDesktopRect(const RECT& virtualDesktopRect) {
    virtualDesktopRect_ = virtualDesktopRect;
    if (!IsInitialized()) {
        return;
    }

    const int width = std::max(1, static_cast<int>(virtualDesktopRect_.right - virtualDesktopRect_.left));
    const int height = std::max(1, static_cast<int>(virtualDesktopRect_.bottom - virtualDesktopRect_.top));
    SetWindowPos(
        window_,
        HWND_BOTTOM,
        virtualDesktopRect_.left,
        virtualDesktopRect_.top,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    EnsureDesktopLayerZOrder();
    ApplyRoundedRegion();
    InvalidateRect(window_, nullptr, TRUE);
    Infrastructure::Logger::Get().Info(
        L"[Overlay] SetVirtualDesktopRect: left=" + std::to_wstring(virtualDesktopRect_.left) +
        L", top=" + std::to_wstring(virtualDesktopRect_.top) +
        L", right=" + std::to_wstring(virtualDesktopRect_.right) +
        L", bottom=" + std::to_wstring(virtualDesktopRect_.bottom));
}

void OverlayWindow::SetDesktopHostWindow(HWND desktopHostWindow) {
    desktopHostWindow_ = desktopHostWindow;
    if (IsInitialized()) {
        EnsureDesktopLayerZOrder();
    }
    std::wstringstream stream;
    stream << L"[Overlay] SetDesktopHostWindow: 0x" << std::hex << std::uppercase
           << reinterpret_cast<uintptr_t>(desktopHostWindow_);
    Infrastructure::Logger::Get().Info(stream.str());
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* overlay = nullptr;
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        overlay = reinterpret_cast<OverlayWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
    } else {
        overlay = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (overlay != nullptr) {
        return overlay->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST:
            if (fixedMode_) {
                return HTTRANSPARENT;
            }
            return HTCLIENT;
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
        case WM_DISPLAYCHANGE:
            if (!IsRectEmpty(&virtualDesktopRect_)) {
                SetVirtualDesktopRect(virtualDesktopRect_);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool OverlayWindow::RegisterClass() const {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &OverlayWindow::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    windowClass.lpszClassName = kOverlayWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            Infrastructure::Logger::Get().Error(
                L"[Overlay] RegisterClassExW failed. error=" + std::to_wstring(error));
            return false;
        }
    }
    return true;
}

bool OverlayWindow::CreateOverlayWindow() {
    if (IsRectEmpty(&virtualDesktopRect_)) {
        virtualDesktopRect_.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        virtualDesktopRect_.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        virtualDesktopRect_.right = virtualDesktopRect_.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        virtualDesktopRect_.bottom = virtualDesktopRect_.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

    const int width = std::max(1, static_cast<int>(virtualDesktopRect_.right - virtualDesktopRect_.left));
    const int height = std::max(1, static_cast<int>(virtualDesktopRect_.bottom - virtualDesktopRect_.top));
    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kOverlayWindowClassName,
        L"WinIconManagement Overlay",
        WS_POPUP,
        virtualDesktopRect_.left,
        virtualDesktopRect_.top,
        width,
        height,
        ownerWindow_,
        nullptr,
        instance_,
        this);

    if (window_ == nullptr) {
        Infrastructure::Logger::Get().Error(
            L"[Overlay] CreateWindowExW failed. error=" + std::to_wstring(GetLastError()));
        return false;
    }

    const BYTE alpha = static_cast<BYTE>(fixedMode_ ? kFenceFillAlpha : (kFenceFillAlpha + 20));
    if (!SetLayeredWindowAttributes(window_, RGB(0, 0, 0), alpha, LWA_ALPHA)) {
        Infrastructure::Logger::Get().Error(
            L"[Overlay] SetLayeredWindowAttributes failed. error=" + std::to_wstring(GetLastError()));
    }
    Infrastructure::Logger::Get().Info(
        L"[Overlay] CreateWindowExW success. hwnd=0x" +
        std::to_wstring(reinterpret_cast<uintptr_t>(window_)) +
        L"; width=" + std::to_wstring(width) +
        L"; height=" + std::to_wstring(height));
    EnsureDesktopLayerZOrder();
    return true;
}

void OverlayWindow::ApplyRoundedRegion() {
    if (!IsInitialized()) {
        return;
    }

    NormalizeFenceRect();
    const int offsetX = fenceRect_.left - virtualDesktopRect_.left;
    const int offsetY = fenceRect_.top - virtualDesktopRect_.top;
    const int width = std::max(1, static_cast<int>(fenceRect_.right - fenceRect_.left));
    const int height = std::max(1, static_cast<int>(fenceRect_.bottom - fenceRect_.top));
    HRGN region = CreateRoundRectRgn(
        offsetX,
        offsetY,
        offsetX + width + 1,
        offsetY + height + 1,
        kFenceCornerRadiusPixels * 2,
        kFenceCornerRadiusPixels * 2);
    if (region == nullptr) {
        Infrastructure::Logger::Get().Error(L"[Overlay] CreateRoundRectRgn failed.");
        return;
    }
    if (SetWindowRgn(window_, region, TRUE) == 0) {
        DeleteObject(region);
        Infrastructure::Logger::Get().Error(
            L"[Overlay] SetWindowRgn failed. error=" + std::to_wstring(GetLastError()));
        return;
    }
    Infrastructure::Logger::Get().Info(
        L"[Overlay] ApplyRoundedRegion: offset=(" + std::to_wstring(offsetX) + L"," +
        std::to_wstring(offsetY) + L"), size=" + std::to_wstring(width) +
        L"x" + std::to_wstring(height));
}

void OverlayWindow::ApplyClickThroughStyle() {
    if (!IsInitialized()) {
        return;
    }

    LONG_PTR extendedStyle = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if (fixedMode_) {
        extendedStyle |= WS_EX_TRANSPARENT;
    } else {
        extendedStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    }

    SetWindowLongPtrW(window_, GWL_EXSTYLE, extendedStyle);
    SetWindowPos(
        window_,
        HWND_BOTTOM,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    EnsureDesktopLayerZOrder();
    Infrastructure::Logger::Get().Info(
        L"[Overlay] ApplyClickThroughStyle: exStyle=" + std::to_wstring(static_cast<unsigned long long>(extendedStyle)));
}

void OverlayWindow::NormalizeFenceRect() {
    if (fenceRect_.left > fenceRect_.right) {
        std::swap(fenceRect_.left, fenceRect_.right);
    }
    if (fenceRect_.top > fenceRect_.bottom) {
        std::swap(fenceRect_.top, fenceRect_.bottom);
    }

    const int minWidth = 120;
    const int minHeight = 80;
    if ((fenceRect_.right - fenceRect_.left) < minWidth) {
        fenceRect_.right = fenceRect_.left + minWidth;
    }
    if ((fenceRect_.bottom - fenceRect_.top) < minHeight) {
        fenceRect_.bottom = fenceRect_.top + minHeight;
    }

    if (!IsRectEmpty(&virtualDesktopRect_)) {
        const LONG desktopRight = virtualDesktopRect_.right;
        const LONG desktopBottom = virtualDesktopRect_.bottom;
        fenceRect_.left = std::max(fenceRect_.left, virtualDesktopRect_.left);
        fenceRect_.top = std::max(fenceRect_.top, virtualDesktopRect_.top);
        fenceRect_.right = std::min(fenceRect_.right, desktopRight);
        fenceRect_.bottom = std::min(fenceRect_.bottom, desktopBottom);
    }
}

void OverlayWindow::EnsureDesktopLayerZOrder() const {
    if (!IsInitialized()) {
        return;
    }
    // Keep overlay in desktop layer: above wallpaper but not above normal app windows.
    HWND insertAfter = HWND_BOTTOM;
    if (desktopHostWindow_ != nullptr && IsWindow(desktopHostWindow_)) {
        insertAfter = desktopHostWindow_;
    } else if (ownerWindow_ != nullptr && IsWindow(ownerWindow_)) {
        insertAfter = ownerWindow_;
    }
    SetWindowPos(
        window_,
        insertAfter,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
}

void OverlayWindow::Paint(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    // Keep background transparent; we only draw fence region.
    HBRUSH clearBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &clientRect, clearBrush);
    DeleteObject(clearBrush);

    const int offsetX = fenceRect_.left - virtualDesktopRect_.left;
    const int offsetY = fenceRect_.top - virtualDesktopRect_.top;
    RECT localFenceRect{
        offsetX,
        offsetY,
        offsetX + std::max(1, static_cast<int>(fenceRect_.right - fenceRect_.left)),
        offsetY + std::max(1, static_cast<int>(fenceRect_.bottom - fenceRect_.top))};

    HBRUSH fillBrush = CreateSolidBrush(kFenceFillColor);
    HPEN borderPen = CreatePen(PS_SOLID, kFenceBorderWidth, kFenceBorderColor);
    HGDIOBJ oldBrush = SelectObject(hdc, fillBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);

    RoundRect(
        hdc,
        localFenceRect.left,
        localFenceRect.top,
        localFenceRect.right,
        localFenceRect.bottom,
        kFenceCornerRadiusPixels * 2,
        kFenceCornerRadiusPixels * 2);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT titleRect{
        localFenceRect.left + 12,
        localFenceRect.top + 10,
        localFenceRect.right - 12,
        localFenceRect.top + 32};
    DrawTextW(hdc, L"Desktop Group", -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    EndPaint(hwnd, &paint);

    const BYTE alpha = static_cast<BYTE>(fixedMode_ ? kFenceFillAlpha : (kFenceFillAlpha + 20));
    SetLayeredWindowAttributes(window_, RGB(0, 0, 0), alpha, LWA_ALPHA);

    if (!paintLogged_) {
        paintLogged_ = true;
        Infrastructure::Logger::Get().Info(
            L"[Overlay] WM_PAINT first frame. clientSize=" +
            std::to_wstring(clientRect.right - clientRect.left) + L"x" +
            std::to_wstring(clientRect.bottom - clientRect.top) +
            L"; alpha=" + std::to_wstring(alpha));
    }
}
}  // namespace Overlay
