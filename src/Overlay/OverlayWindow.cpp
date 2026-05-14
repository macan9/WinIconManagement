#include "Overlay/OverlayWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <string>
#include <sstream>
#include <utility>

#include "Infrastructure/Logger.h"

namespace {
constexpr wchar_t kOverlayWindowClassName[] = L"WinIconManagement.OverlayWindow";
constexpr int kFenceCornerRadiusPixels = 10;
constexpr COLORREF kFenceFillColor = RGB(76, 143, 255);
constexpr COLORREF kFenceBorderColor = RGB(36, 99, 235);
constexpr COLORREF kFenceActiveFillColor = RGB(34, 197, 94);
constexpr COLORREF kFenceActiveBorderColor = RGB(21, 128, 61);
constexpr BYTE kFenceFillAlpha = 70;
constexpr int kFenceBorderWidth = 2;
constexpr int kFenceActiveBorderWidth = 3;
constexpr COLORREF kSelectionFillColor = RGB(59, 130, 246);
constexpr COLORREF kSelectionBorderColor = RGB(37, 99, 235);
constexpr int kSelectionBorderWidth = 2;
constexpr BYTE kSelectionOnlyAlpha = 140;
constexpr int kConfirmPopupWidth = 240;
constexpr int kConfirmPopupHeight = 56;
constexpr int kConfirmPopupPadding = 8;
constexpr int kConfirmButtonWidth = 106;
constexpr int kConfirmButtonHeight = 36;
constexpr int kConfirmButtonGap = 10;
constexpr COLORREF kConfirmPanelColor = RGB(19, 33, 58);
constexpr COLORREF kConfirmPanelBorder = RGB(78, 118, 177);
constexpr COLORREF kConfirmButtonColor = RGB(44, 94, 186);
constexpr COLORREF kConfirmButtonHoverColor = RGB(63, 122, 227);
constexpr COLORREF kCancelButtonColor = RGB(67, 76, 93);
constexpr COLORREF kCancelButtonHoverColor = RGB(88, 99, 120);
constexpr COLORREF kConfirmTextColor = RGB(238, 244, 255);
constexpr int kActiveFenceResizeHandleSize = 28;
constexpr int kActiveFenceResizeHandleMargin = 0;
constexpr COLORREF kActiveFenceResizeHandleColor = RGB(236, 253, 245);
constexpr COLORREF kActiveFenceResizeHandleBorderColor = RGB(22, 163, 74);
constexpr int kActiveFenceDeleteButtonSize = 20;
constexpr int kActiveFenceDeleteButtonMargin = 6;
constexpr int kActiveFenceMoveAreaHeight = 4096;
constexpr COLORREF kActiveFenceDeleteButtonColor = RGB(239, 68, 68);
constexpr COLORREF kActiveFenceDeleteButtonHoverColor = RGB(220, 38, 38);
constexpr COLORREF kActiveFenceDeleteGlyphColor = RGB(255, 255, 255);
constexpr int kOverlayMinimumFenceWidth = 120;
constexpr int kOverlayMinimumFenceHeight = 80;
}

namespace Overlay {
OverlayWindow::OverlayWindow()
    : instance_(nullptr),
      ownerWindow_(nullptr),
      window_(nullptr),
      desktopHostWindow_(nullptr),
      virtualDesktopRect_{0, 0, 0, 0},
      fenceRects_(),
      fenceTitles_(),
      activeFenceIndex_(std::nullopt),
      fenceRect_{120, 120, 560, 360},
      selectionRect_{0, 0, 0, 0},
      hasSelectionRect_(false),
      selectionConfirmVisible_(false),
      selectionConfirmAnchor_{0, 0},
      selectionConfirmRect_{0, 0, 0, 0},
      selectionConfirmAction_(SelectionConfirmAction::None),
      selectionConfirmHoverAction_(SelectionConfirmAction::None),
      fixedMode_(true),
      visible_(false),
      paintLogged_(false),
      hoverHitTarget_(InteractionHitTarget::Transparent),
      activeFenceDragMode_(ActiveFenceDragMode::None),
      activeFenceDragStartPoint_{0, 0},
      activeFenceDragStartRect_{0, 0, 0, 0} {}

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

void OverlayWindow::SetFenceRects(const std::vector<RECT>& fenceRects) {
    fenceRects_ = fenceRects;
    NormalizeFenceRects();
    if (!fenceRects_.empty()) {
        fenceRect_ = fenceRects_.front();
    }
    if (IsInitialized()) {
        ApplyRoundedRegion();
        InvalidateRect(window_, nullptr, TRUE);
    }
    Infrastructure::Logger::Get().Info(
        L"[Overlay] SetFenceRects: count=" + std::to_wstring(fenceRects_.size()));
}

void OverlayWindow::SetFenceRect(const RECT& fenceRect) {
    fenceRect_ = fenceRect;
    NormalizeFenceRect();
    fenceRects_.clear();
    fenceRects_.push_back(fenceRect_);
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

void OverlayWindow::SetFencePresentation(
    const std::vector<std::wstring>& fenceTitles,
    std::optional<size_t> activeFenceIndex) {
    fenceTitles_ = fenceTitles;
    activeFenceIndex_ = activeFenceIndex;
    if (activeFenceIndex_.has_value() && *activeFenceIndex_ >= fenceRects_.size()) {
        activeFenceIndex_.reset();
    }
    if (IsInitialized()) {
        InvalidateRect(window_, nullptr, TRUE);
    }
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

void OverlayWindow::SetSelectionRect(const RECT& selectionRect) {
    selectionRect_ = selectionRect;
    hasSelectionRect_ = true;
    NormalizeSelectionRect();
    if (IsInitialized()) {
        ApplyRoundedRegion();
        InvalidateRect(window_, nullptr, TRUE);
    }
}

void OverlayWindow::ClearSelectionRect() {
    if (!hasSelectionRect_ && !selectionConfirmVisible_) {
        return;
    }
    hasSelectionRect_ = false;
    selectionConfirmVisible_ = false;
    selectionConfirmAction_ = SelectionConfirmAction::None;
    selectionConfirmHoverAction_ = SelectionConfirmAction::None;
    selectionRect_ = RECT{0, 0, 0, 0};
    selectionConfirmRect_ = RECT{0, 0, 0, 0};
    hoverHitTarget_ = InteractionHitTarget::Transparent;
    if (IsInitialized()) {
        ApplyClickThroughStyle();
        ApplyRoundedRegion();
        InvalidateRect(window_, nullptr, TRUE);
    }
}

void OverlayWindow::ShowSelectionConfirm(const RECT& selectionRect, const POINT& anchorPoint) {
    selectionRect_ = selectionRect;
    NormalizeSelectionRect();
    hasSelectionRect_ = true;
    selectionConfirmVisible_ = true;
    selectionConfirmAnchor_ = anchorPoint;
    selectionConfirmRect_ = BuildConfirmRect();
    selectionConfirmAction_ = SelectionConfirmAction::None;
    selectionConfirmHoverAction_ = SelectionConfirmAction::None;
    hoverHitTarget_ = InteractionHitTarget::Transparent;
    if (IsInitialized()) {
        ApplyClickThroughStyle();
        ApplyRoundedRegion();
        InvalidateRect(window_, nullptr, TRUE);
    }
}

void OverlayWindow::HideSelectionConfirm() {
    if (!selectionConfirmVisible_) {
        return;
    }
    selectionConfirmVisible_ = false;
    selectionConfirmAction_ = SelectionConfirmAction::None;
    selectionConfirmHoverAction_ = SelectionConfirmAction::None;
    selectionConfirmRect_ = RECT{0, 0, 0, 0};
    hoverHitTarget_ = InteractionHitTarget::Transparent;
    if (IsInitialized()) {
        ApplyClickThroughStyle();
        ApplyRoundedRegion();
        InvalidateRect(window_, nullptr, TRUE);
    }
}

bool OverlayWindow::IsSelectionConfirmVisible() const {
    return selectionConfirmVisible_;
}

bool OverlayWindow::IsPointInSelectionConfirm(POINT screenPoint) const {
    if (!selectionConfirmVisible_) {
        return false;
    }
    const RECT confirmRect = BuildConfirmRect();
    const POINT localPoint{
        screenPoint.x - virtualDesktopRect_.left,
        screenPoint.y - virtualDesktopRect_.top};
    return PtInRect(&confirmRect, localPoint) != FALSE;
}

bool OverlayWindow::HandleSelectionConfirmClick(WPARAM message, POINT screenPoint) {
    if (!selectionConfirmVisible_) {
        return false;
    }
    const POINT localPoint{
        screenPoint.x - virtualDesktopRect_.left,
        screenPoint.y - virtualDesktopRect_.top};

    switch (message) {
        case WM_MOUSEMOVE: {
            const SelectionConfirmAction hovered = HitTestConfirmAction(localPoint);
            if (hovered != selectionConfirmHoverAction_) {
                selectionConfirmHoverAction_ = hovered;
                if (IsInitialized()) {
                    InvalidateRect(window_, nullptr, TRUE);
                }
            }
            return true;
        }
        case WM_LBUTTONDOWN:
            SetSelectionConfirmAction(HitTestConfirmAction(localPoint));
            return true;
        case WM_LBUTTONUP: {
            const SelectionConfirmAction hitAction = HitTestConfirmAction(localPoint);
            if (hitAction != SelectionConfirmAction::None && hitAction == selectionConfirmAction_) {
                const bool confirmed = hitAction == SelectionConfirmAction::Confirm;
                selectionConfirmAction_ = SelectionConfirmAction::None;
                HideSelectionConfirm();
                if (onSelectionConfirm_) {
                    onSelectionConfirm_(confirmed);
                }
                return true;
            }
            selectionConfirmAction_ = SelectionConfirmAction::None;
            return true;
        }
        default:
            return false;
    }
}

void OverlayWindow::SetSelectionConfirmCallback(SelectionConfirmCallback onSelectionConfirm) {
    onSelectionConfirm_ = std::move(onSelectionConfirm);
}

bool OverlayWindow::HandleActiveFenceResizeMouse(WPARAM message, POINT screenPoint) {
    if (fixedMode_ || selectionConfirmVisible_) {
        return false;
    }

    switch (message) {
        case WM_MOUSEMOVE:
            if (activeFenceDragMode_ != ActiveFenceDragMode::None) {
                UpdateActiveFenceInteraction(screenPoint);
                return true;
            }
            UpdateInteractionHoverState(screenPoint);
            return false;
        case WM_LBUTTONDOWN:
            switch (HitTestInteractiveTarget(screenPoint)) {
                case InteractionHitTarget::ActiveFenceResizeHandle:
                    BeginActiveFenceInteraction(screenPoint, ActiveFenceDragMode::Resize);
                    return true;
                case InteractionHitTarget::ActiveFenceMoveArea:
                    BeginActiveFenceInteraction(screenPoint, ActiveFenceDragMode::Move);
                    return true;
                case InteractionHitTarget::ActiveFenceDeleteButton:
                    if (onActiveFenceDelete_) {
                        onActiveFenceDelete_();
                    }
                    return true;
                default:
                    return false;
            }
        case WM_LBUTTONUP:
            if (activeFenceDragMode_ == ActiveFenceDragMode::None) {
                return false;
            }
            FinishActiveFenceInteraction(true);
            return true;
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (activeFenceDragMode_ == ActiveFenceDragMode::None) {
                return false;
            }
            FinishActiveFenceInteraction(false);
            return true;
        default:
            return false;
    }
}

void OverlayWindow::SetActiveFenceResizeCallback(ActiveFenceResizeCallback onActiveFenceResize) {
    onActiveFenceResize_ = std::move(onActiveFenceResize);
}

void OverlayWindow::SetActiveFenceDeleteCallback(ActiveFenceDeleteCallback onActiveFenceDelete) {
    onActiveFenceDelete_ = std::move(onActiveFenceDelete);
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
            return HTTRANSPARENT;
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
    if (!SetLayeredWindowAttributes(window_, RGB(0, 0, 0), alpha, LWA_ALPHA | LWA_COLORKEY)) {
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
    NormalizeFenceRects();
}

void OverlayWindow::ApplyClickThroughStyle() {
    if (!IsInitialized()) {
        return;
    }

    LONG_PTR extendedStyle = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    extendedStyle |= WS_EX_TRANSPARENT;

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

void OverlayWindow::NormalizeFenceRects() {
    if (fenceRects_.empty()) {
        fenceRects_.push_back(fenceRect_);
    }

    for (RECT& fenceRect : fenceRects_) {
        fenceRect_ = fenceRect;
        NormalizeFenceRect();
        fenceRect = fenceRect_;
    }
    fenceRect_ = fenceRects_.front();
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
    // Fill with colorkey so untouched overlay area stays visually transparent.
    HBRUSH clearBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &clientRect, clearBrush);
    DeleteObject(clearBrush);

    if (!hasSelectionRect_) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        for (size_t i = 0; i < fenceRects_.size(); ++i) {
            const RECT& fenceRect = fenceRects_[i];
            const bool isActiveFence = activeFenceIndex_.has_value() && *activeFenceIndex_ == i;
            const int offsetX = fenceRect.left - virtualDesktopRect_.left;
            const int offsetY = fenceRect.top - virtualDesktopRect_.top;
            RECT localFenceRect{
                offsetX,
                offsetY,
                offsetX + std::max(1, static_cast<int>(fenceRect.right - fenceRect.left)),
                offsetY + std::max(1, static_cast<int>(fenceRect.bottom - fenceRect.top))};

            const COLORREF fillColor = isActiveFence ? kFenceActiveFillColor : kFenceFillColor;
            const COLORREF borderColor = isActiveFence ? kFenceActiveBorderColor : kFenceBorderColor;
            const int borderWidth = isActiveFence ? kFenceActiveBorderWidth : kFenceBorderWidth;

            HBRUSH fillBrush = CreateSolidBrush(fillColor);
            HPEN borderPen = CreatePen(PS_SOLID, borderWidth, borderColor);
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

            RECT titleRect{
                localFenceRect.left + 12,
                localFenceRect.top + 10,
                localFenceRect.right - 12,
                localFenceRect.top + 32};
            std::wstring title = L"Desktop Group";
            if (i < fenceTitles_.size() && !fenceTitles_[i].empty()) {
                title = fenceTitles_[i];
            }
            if (isActiveFence) {
                title += L" [Active]";
            }
            DrawTextW(hdc, title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }

        if (!fixedMode_ && !hasSelectionRect_ && activeFenceIndex_.has_value()) {
            DrawActiveFenceDeleteButton(hdc);
            DrawActiveFenceResizeHandle(hdc);
        }
    }

    if (hasSelectionRect_) {
        RECT localSelection{
            selectionRect_.left - virtualDesktopRect_.left,
            selectionRect_.top - virtualDesktopRect_.top,
            selectionRect_.right - virtualDesktopRect_.left,
            selectionRect_.bottom - virtualDesktopRect_.top};

        HBRUSH selectionFill = CreateSolidBrush(kSelectionFillColor);
        HPEN selectionBorder = CreatePen(PS_SOLID, kSelectionBorderWidth, kSelectionBorderColor);
        HGDIOBJ oldSelectionBrush = SelectObject(hdc, selectionFill);
        HGDIOBJ oldSelectionPen = SelectObject(hdc, selectionBorder);
        Rectangle(
            hdc,
            localSelection.left,
            localSelection.top,
            localSelection.right,
            localSelection.bottom);
        SelectObject(hdc, oldSelectionPen);
        SelectObject(hdc, oldSelectionBrush);
        DeleteObject(selectionBorder);
        DeleteObject(selectionFill);
    }

    if (selectionConfirmVisible_) {
        DrawConfirmUI(hdc);
    }

    EndPaint(hwnd, &paint);

    BYTE alpha = static_cast<BYTE>(fixedMode_ ? kFenceFillAlpha : (kFenceFillAlpha + 20));
    if (hasSelectionRect_) {
        alpha = std::max(alpha, kSelectionOnlyAlpha);
    }
    SetLayeredWindowAttributes(window_, RGB(0, 0, 0), alpha, LWA_ALPHA | LWA_COLORKEY);

    if (!paintLogged_) {
        paintLogged_ = true;
        Infrastructure::Logger::Get().Info(
            L"[Overlay] WM_PAINT first frame. clientSize=" +
            std::to_wstring(clientRect.right - clientRect.left) + L"x" +
            std::to_wstring(clientRect.bottom - clientRect.top) +
            L"; alpha=" + std::to_wstring(alpha));
    }
}

void OverlayWindow::NormalizeSelectionRect() {
    if (selectionRect_.left > selectionRect_.right) {
        std::swap(selectionRect_.left, selectionRect_.right);
    }
    if (selectionRect_.top > selectionRect_.bottom) {
        std::swap(selectionRect_.top, selectionRect_.bottom);
    }
}

RECT OverlayWindow::BuildConfirmRect() const {
    POINT localAnchor{
        selectionConfirmAnchor_.x - virtualDesktopRect_.left,
        selectionConfirmAnchor_.y - virtualDesktopRect_.top};
    RECT rect{
        localAnchor.x + 12,
        localAnchor.y + 12,
        localAnchor.x + 12 + kConfirmPopupWidth,
        localAnchor.y + 12 + kConfirmPopupHeight};

    RECT clientRect{};
    if (window_ != nullptr) {
        GetClientRect(window_, &clientRect);
    } else {
        clientRect.right = std::max(1L, virtualDesktopRect_.right - virtualDesktopRect_.left);
        clientRect.bottom = std::max(1L, virtualDesktopRect_.bottom - virtualDesktopRect_.top);
    }

    if (rect.right > clientRect.right) {
        const int delta = rect.right - clientRect.right;
        rect.left -= delta;
        rect.right -= delta;
    }
    if (rect.left < 0) {
        rect.right -= rect.left;
        rect.left = 0;
    }
    if (rect.bottom > clientRect.bottom) {
        const int delta = rect.bottom - clientRect.bottom;
        rect.top -= delta;
        rect.bottom -= delta;
    }
    if (rect.top < 0) {
        rect.bottom -= rect.top;
        rect.top = 0;
    }
    return rect;
}

RECT OverlayWindow::BuildConfirmButtonRect() const {
    RECT rect = selectionConfirmRect_;
    const int top = rect.top + (kConfirmPopupHeight - kConfirmButtonHeight) / 2;
    return RECT{
        rect.left + kConfirmPopupPadding,
        top,
        rect.left + kConfirmPopupPadding + kConfirmButtonWidth,
        top + kConfirmButtonHeight};
}

RECT OverlayWindow::BuildCancelButtonRect() const {
    RECT rect = selectionConfirmRect_;
    const int top = rect.top + (kConfirmPopupHeight - kConfirmButtonHeight) / 2;
    const int left = rect.left + kConfirmPopupPadding + kConfirmButtonWidth + kConfirmButtonGap;
    return RECT{
        left,
        top,
        left + kConfirmButtonWidth,
        top + kConfirmButtonHeight};
}

void OverlayWindow::DrawConfirmUI(HDC hdc) const {
    if (!selectionConfirmVisible_) {
        return;
    }

    HBRUSH panelBrush = CreateSolidBrush(kConfirmPanelColor);
    HPEN panelPen = CreatePen(PS_SOLID, 1, kConfirmPanelBorder);
    HGDIOBJ oldBrush = SelectObject(hdc, panelBrush);
    HGDIOBJ oldPen = SelectObject(hdc, panelPen);
    RoundRect(
        hdc,
        selectionConfirmRect_.left,
        selectionConfirmRect_.top,
        selectionConfirmRect_.right,
        selectionConfirmRect_.bottom,
        14,
        14);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(panelPen);
    DeleteObject(panelBrush);

    const RECT confirmButton = BuildConfirmButtonRect();
    const bool confirmHover = selectionConfirmHoverAction_ == SelectionConfirmAction::Confirm;
    HBRUSH confirmBrush = CreateSolidBrush(confirmHover ? kConfirmButtonHoverColor : kConfirmButtonColor);
    HPEN confirmPen = CreatePen(PS_SOLID, 1, confirmHover ? kConfirmButtonHoverColor : kConfirmButtonColor);
    oldBrush = SelectObject(hdc, confirmBrush);
    oldPen = SelectObject(hdc, confirmPen);
    RoundRect(
        hdc,
        confirmButton.left,
        confirmButton.top,
        confirmButton.right,
        confirmButton.bottom,
        10,
        10);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(confirmPen);
    DeleteObject(confirmBrush);

    const RECT cancelButton = BuildCancelButtonRect();
    const bool cancelHover = selectionConfirmHoverAction_ == SelectionConfirmAction::Cancel;
    HBRUSH cancelBrush = CreateSolidBrush(cancelHover ? kCancelButtonHoverColor : kCancelButtonColor);
    HPEN cancelPen = CreatePen(PS_SOLID, 1, cancelHover ? kCancelButtonHoverColor : kCancelButtonColor);
    oldBrush = SelectObject(hdc, cancelBrush);
    oldPen = SelectObject(hdc, cancelPen);
    RoundRect(
        hdc,
        cancelButton.left,
        cancelButton.top,
        cancelButton.right,
        cancelButton.bottom,
        10,
        10);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(cancelPen);
    DeleteObject(cancelBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kConfirmTextColor);
    DrawTextW(hdc, L"\u786e\u5b9a\u7ed8\u5236", -1, const_cast<RECT*>(&confirmButton), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, L"\u53d6\u6d88\u7ed8\u5236", -1, const_cast<RECT*>(&cancelButton), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void OverlayWindow::DrawActiveFenceResizeHandle(HDC hdc) const {
    const std::optional<RECT> activeFenceRect = GetActiveFenceRect();
    if (!activeFenceRect.has_value()) {
        return;
    }

    const RECT handleRect = BuildActiveFenceResizeHandleRect(*activeFenceRect);
    HBRUSH fillBrush = CreateSolidBrush(kActiveFenceResizeHandleColor);
    HPEN borderPen = CreatePen(PS_SOLID, 1, kActiveFenceResizeHandleBorderColor);
    HGDIOBJ oldBrush = SelectObject(hdc, fillBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    Rectangle(hdc, handleRect.left, handleRect.top, handleRect.right, handleRect.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);
}

void OverlayWindow::DrawActiveFenceDeleteButton(HDC hdc) const {
    const std::optional<RECT> activeFenceRect = GetActiveFenceRect();
    if (!activeFenceRect.has_value()) {
        return;
    }

    const RECT buttonRect = BuildActiveFenceDeleteButtonRect(*activeFenceRect);
    const bool hovered = hoverHitTarget_ == InteractionHitTarget::ActiveFenceDeleteButton;
    HBRUSH fillBrush = CreateSolidBrush(hovered ? kActiveFenceDeleteButtonHoverColor : kActiveFenceDeleteButtonColor);
    HPEN borderPen = CreatePen(PS_SOLID, 1, hovered ? kActiveFenceDeleteButtonHoverColor : kActiveFenceDeleteButtonColor);
    HGDIOBJ oldBrush = SelectObject(hdc, fillBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    RoundRect(hdc, buttonRect.left, buttonRect.top, buttonRect.right, buttonRect.bottom, 8, 8);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);

    HPEN glyphPen = CreatePen(PS_SOLID, 2, kActiveFenceDeleteGlyphColor);
    oldPen = SelectObject(hdc, glyphPen);
    MoveToEx(hdc, buttonRect.left + 6, buttonRect.top + 6, nullptr);
    LineTo(hdc, buttonRect.right - 6, buttonRect.bottom - 6);
    MoveToEx(hdc, buttonRect.right - 6, buttonRect.top + 6, nullptr);
    LineTo(hdc, buttonRect.left + 6, buttonRect.bottom - 6);
    SelectObject(hdc, oldPen);
    DeleteObject(glyphPen);
}

OverlayWindow::SelectionConfirmAction OverlayWindow::HitTestConfirmAction(POINT localPoint) const {
    if (!selectionConfirmVisible_) {
        return SelectionConfirmAction::None;
    }

    RECT confirmButton = BuildConfirmButtonRect();
    if (PtInRect(&confirmButton, localPoint) != FALSE) {
        return SelectionConfirmAction::Confirm;
    }
    RECT cancelButton = BuildCancelButtonRect();
    if (PtInRect(&cancelButton, localPoint) != FALSE) {
        return SelectionConfirmAction::Cancel;
    }
    return SelectionConfirmAction::None;
}

void OverlayWindow::SetSelectionConfirmAction(SelectionConfirmAction action) {
    selectionConfirmAction_ = action;
}

std::optional<RECT> OverlayWindow::GetActiveFenceRect() const {
    if (!activeFenceIndex_.has_value() || *activeFenceIndex_ >= fenceRects_.size()) {
        return std::nullopt;
    }
    return fenceRects_[*activeFenceIndex_];
}

RECT OverlayWindow::BuildActiveFenceResizeHandleRect(const RECT& activeFenceRect) const {
    const int offsetX = activeFenceRect.left - virtualDesktopRect_.left;
    const int offsetY = activeFenceRect.top - virtualDesktopRect_.top;
    const int width = std::max(1, static_cast<int>(activeFenceRect.right - activeFenceRect.left));
    const int height = std::max(1, static_cast<int>(activeFenceRect.bottom - activeFenceRect.top));
    const int right = offsetX + width - kActiveFenceResizeHandleMargin;
    const int bottom = offsetY + height - kActiveFenceResizeHandleMargin;
    return RECT{
        right - kActiveFenceResizeHandleSize,
        bottom - kActiveFenceResizeHandleSize,
        right,
        bottom};
}

RECT OverlayWindow::BuildActiveFenceDeleteButtonRect(const RECT& activeFenceRect) const {
    const int offsetX = activeFenceRect.left - virtualDesktopRect_.left;
    const int offsetY = activeFenceRect.top - virtualDesktopRect_.top;
    return RECT{
        offsetX + std::max(0, static_cast<int>(activeFenceRect.right - activeFenceRect.left) -
                                  kActiveFenceDeleteButtonMargin - kActiveFenceDeleteButtonSize),
        offsetY + kActiveFenceDeleteButtonMargin,
        offsetX + std::max(0, static_cast<int>(activeFenceRect.right - activeFenceRect.left) -
                                  kActiveFenceDeleteButtonMargin),
        offsetY + kActiveFenceDeleteButtonMargin + kActiveFenceDeleteButtonSize};
}

OverlayWindow::InteractionHitTarget OverlayWindow::HitTestInteractiveTarget(POINT screenPoint) const {
    if (selectionConfirmVisible_ && IsPointInSelectionConfirm(screenPoint)) {
        return InteractionHitTarget::SelectionConfirm;
    }

    if (fixedMode_ || selectionConfirmVisible_) {
        return InteractionHitTarget::Transparent;
    }

    const std::optional<RECT> activeFenceRect = GetActiveFenceRect();
    if (!activeFenceRect.has_value()) {
        return InteractionHitTarget::Transparent;
    }

    const POINT localPoint{
        screenPoint.x - virtualDesktopRect_.left,
        screenPoint.y - virtualDesktopRect_.top};
    const RECT deleteButtonRect = BuildActiveFenceDeleteButtonRect(*activeFenceRect);
    if (PtInRect(&deleteButtonRect, localPoint) != FALSE) {
        return InteractionHitTarget::ActiveFenceDeleteButton;
    }

    const RECT handleRect = BuildActiveFenceResizeHandleRect(*activeFenceRect);
    if (PtInRect(&handleRect, localPoint) != FALSE) {
        return InteractionHitTarget::ActiveFenceResizeHandle;
    }

    const int offsetX = activeFenceRect->left - virtualDesktopRect_.left;
    const int offsetY = activeFenceRect->top - virtualDesktopRect_.top;
    RECT moveRect{
        offsetX,
        offsetY,
        offsetX + std::max(1, static_cast<int>(activeFenceRect->right - activeFenceRect->left)),
        offsetY + std::min(
                      kActiveFenceMoveAreaHeight,
                      std::max(1, static_cast<int>(activeFenceRect->bottom - activeFenceRect->top)))};
    if (PtInRect(&moveRect, localPoint) != FALSE) {
        return InteractionHitTarget::ActiveFenceMoveArea;
    }
    return InteractionHitTarget::Transparent;
}

void OverlayWindow::UpdateInteractionHoverState(POINT screenPoint) {
    const InteractionHitTarget hitTarget = HitTestInteractiveTarget(screenPoint);
    if (hitTarget == hoverHitTarget_) {
        return;
    }

    hoverHitTarget_ = hitTarget;
    if (IsInitialized()) {
        InvalidateRect(window_, nullptr, TRUE);
    }
}

void OverlayWindow::BeginActiveFenceInteraction(POINT screenPoint, ActiveFenceDragMode dragMode) {
    const std::optional<RECT> activeFenceRect = GetActiveFenceRect();
    if (!activeFenceRect.has_value()) {
        return;
    }

    activeFenceDragMode_ = dragMode;
    activeFenceDragStartPoint_ = screenPoint;
    activeFenceDragStartRect_ = *activeFenceRect;
    SetCapture(window_);
    ApplyClickThroughStyle();
    Infrastructure::Logger::Get().Info(
        L"[Overlay] active fence interaction begin. mode=" +
        std::to_wstring(static_cast<int>(activeFenceDragMode_)) +
        L"; left=" + std::to_wstring(activeFenceDragStartRect_.left) +
        L", top=" + std::to_wstring(activeFenceDragStartRect_.top) +
        L", right=" + std::to_wstring(activeFenceDragStartRect_.right) +
        L", bottom=" + std::to_wstring(activeFenceDragStartRect_.bottom));
}

void OverlayWindow::UpdateActiveFenceInteraction(POINT screenPoint) {
    if (activeFenceDragMode_ == ActiveFenceDragMode::None || !activeFenceIndex_.has_value() ||
        *activeFenceIndex_ >= fenceRects_.size()) {
        return;
    }

    RECT updatedRect = activeFenceDragStartRect_;
    const int deltaX = screenPoint.x - activeFenceDragStartPoint_.x;
    const int deltaY = screenPoint.y - activeFenceDragStartPoint_.y;
    if (activeFenceDragMode_ == ActiveFenceDragMode::Resize) {
        updatedRect.right = activeFenceDragStartRect_.right + deltaX;
        updatedRect.bottom = activeFenceDragStartRect_.bottom + deltaY;
        updatedRect.right = std::min(
            virtualDesktopRect_.right,
            std::max(updatedRect.left + kOverlayMinimumFenceWidth, updatedRect.right));
        updatedRect.bottom = std::min(
            virtualDesktopRect_.bottom,
            std::max(updatedRect.top + kOverlayMinimumFenceHeight, updatedRect.bottom));
    } else {
        const int width = activeFenceDragStartRect_.right - activeFenceDragStartRect_.left;
        const int height = activeFenceDragStartRect_.bottom - activeFenceDragStartRect_.top;
        const LONG maxLeft = std::max(virtualDesktopRect_.left, virtualDesktopRect_.right - width);
        const LONG maxTop = std::max(virtualDesktopRect_.top, virtualDesktopRect_.bottom - height);
        updatedRect.left = std::max(
            virtualDesktopRect_.left,
            std::min(maxLeft, activeFenceDragStartRect_.left + deltaX));
        updatedRect.top = std::max(
            virtualDesktopRect_.top,
            std::min(maxTop, activeFenceDragStartRect_.top + deltaY));
        updatedRect.right = updatedRect.left + width;
        updatedRect.bottom = updatedRect.top + height;
    }

    fenceRects_[*activeFenceIndex_] = updatedRect;
    if (*activeFenceIndex_ == 0) {
        fenceRect_ = updatedRect;
    }
    if (IsInitialized()) {
        InvalidateRect(window_, nullptr, TRUE);
    }
}

void OverlayWindow::FinishActiveFenceInteraction(bool commitChanges) {
    if (activeFenceDragMode_ == ActiveFenceDragMode::None) {
        return;
    }

    const ActiveFenceDragMode dragMode = activeFenceDragMode_;
    activeFenceDragMode_ = ActiveFenceDragMode::None;
    if (GetCapture() == window_) {
        ReleaseCapture();
    }

    if (!activeFenceIndex_.has_value() || *activeFenceIndex_ >= fenceRects_.size()) {
        ApplyClickThroughStyle();
        return;
    }

    if (!commitChanges) {
        fenceRects_[*activeFenceIndex_] = activeFenceDragStartRect_;
        if (*activeFenceIndex_ == 0) {
            fenceRect_ = activeFenceDragStartRect_;
        }
    } else if (onActiveFenceResize_) {
        onActiveFenceResize_(fenceRects_[*activeFenceIndex_]);
    }

    ApplyClickThroughStyle();
    if (IsInitialized()) {
        InvalidateRect(window_, nullptr, TRUE);
    }
    Infrastructure::Logger::Get().Info(
        L"[Overlay] active fence interaction finish. mode=" +
        std::to_wstring(static_cast<int>(dragMode)) +
        L"; commit=" + std::wstring(commitChanges ? L"true" : L"false"));
}
}  // namespace Overlay
