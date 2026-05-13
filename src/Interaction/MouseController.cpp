#include "Interaction/MouseController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <sstream>

#include "Infrastructure/Logger.h"

namespace {
constexpr int kDragThresholdPixels = 12;

Interaction::MouseController* g_activeController = nullptr;

RECT MakeNormalizedRect(const POINT& a, const POINT& b) {
    RECT rect{};
    rect.left = std::min(a.x, b.x);
    rect.top = std::min(a.y, b.y);
    rect.right = std::max(a.x, b.x);
    rect.bottom = std::max(a.y, b.y);
    return rect;
}
}  // namespace

namespace Interaction {
MouseController::MouseController()
    : hook_(nullptr),
      desktopListViewWindow_(nullptr),
      enabled_(false),
      rightButtonDown_(false),
      selectionActive_(false),
      downPoint_{0, 0},
      lastPoint_{0, 0} {}

MouseController::~MouseController() {
    Stop();
}

bool MouseController::Start() {
    if (hook_ != nullptr) {
        return true;
    }

    if (g_activeController != nullptr && g_activeController != this) {
        return false;
    }

    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, HookProc, GetModuleHandleW(nullptr), 0);
    if (hook == nullptr) {
        Infrastructure::Logger::Get().Error(
            L"[Selection] failed to install WH_MOUSE_LL. error=" +
            std::to_wstring(GetLastError()));
        return false;
    }

    hook_ = hook;
    g_activeController = this;
    Infrastructure::Logger::Get().Info(L"[Selection] WH_MOUSE_LL installed.");
    return true;
}

void MouseController::Stop() {
    if (hook_ != nullptr) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    if (g_activeController == this) {
        g_activeController = nullptr;
    }
    ResetDragState();
}

void MouseController::SetEnabled(bool enabled) {
    enabled_ = enabled;
    Infrastructure::Logger::Get().Info(
        L"[Selection] mouse controller enabled=" + std::wstring(enabled_ ? L"true" : L"false"));
    if (!enabled_) {
        ResetDragState();
    }
}

void MouseController::SetDesktopListViewWindow(HWND listViewWindow) {
    desktopListViewWindow_ = listViewWindow;
    std::wstringstream stream;
    stream << L"[Selection] desktop list-view hwnd=0x" << std::hex << std::uppercase
           << reinterpret_cast<uintptr_t>(desktopListViewWindow_);
    Infrastructure::Logger::Get().Info(stream.str());
}

void MouseController::SetCallbacks(
    SelectionStartedCallback onStarted,
    SelectionUpdatedCallback onUpdated,
    SelectionCompletedCallback onCompleted,
    SelectionCanceledCallback onCanceled) {
    onStarted_ = std::move(onStarted);
    onUpdated_ = std::move(onUpdated);
    onCompleted_ = std::move(onCompleted);
    onCanceled_ = std::move(onCanceled);
}

void MouseController::SetMouseEventFilterCallback(MouseEventFilterCallback onFilter) {
    onMouseEventFilter_ = std::move(onFilter);
}

LRESULT CALLBACK MouseController::HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_activeController != nullptr && lParam != 0) {
        const auto* data = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (g_activeController->HandleMouseEvent(wParam, data)) {
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool MouseController::IsPointOnDesktop(POINT screenPoint) const {
    if (desktopListViewWindow_ == nullptr || !IsWindow(desktopListViewWindow_)) {
        return false;
    }

    RECT listViewRect{};
    if (GetWindowRect(desktopListViewWindow_, &listViewRect) != FALSE &&
        PtInRect(&listViewRect, screenPoint) != FALSE) {
        return true;
    }

    HWND underCursor = WindowFromPoint(screenPoint);
    if (underCursor == nullptr) {
        return false;
    }

    if (underCursor == desktopListViewWindow_ || IsChild(desktopListViewWindow_, underCursor) != FALSE) {
        return true;
    }

    HWND current = underCursor;
    while (current != nullptr) {
        if (current == desktopListViewWindow_) {
            return true;
        }
        current = GetParent(current);
    }
    return false;
}

void MouseController::OpenDesktopContextMenu(POINT screenPoint) const {
    if (desktopListViewWindow_ == nullptr || !IsWindow(desktopListViewWindow_)) {
        return;
    }

    LPARAM position = MAKELPARAM(static_cast<SHORT>(screenPoint.x), static_cast<SHORT>(screenPoint.y));
    SendMessageW(desktopListViewWindow_, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(desktopListViewWindow_), position);
}

void MouseController::ResetDragState() {
    const bool hadSelection = selectionActive_;
    rightButtonDown_ = false;
    selectionActive_ = false;
    if (hadSelection && onCanceled_) {
        onCanceled_();
    }
}

bool MouseController::HandleMouseEvent(WPARAM wParam, const MSLLHOOKSTRUCT* data) {
    if (data == nullptr || !enabled_) {
        return false;
    }

    const POINT point = data->pt;
    if (onMouseEventFilter_ && onMouseEventFilter_(wParam, point)) {
        return true;
    }

    switch (wParam) {
        case WM_RBUTTONDOWN:
            if (!IsPointOnDesktop(point)) {
                return false;
            }
            rightButtonDown_ = true;
            selectionActive_ = false;
            downPoint_ = point;
            lastPoint_ = point;
            return true;
        case WM_MOUSEMOVE:
            if (!rightButtonDown_) {
                return false;
            }
            lastPoint_ = point;
            if (!selectionActive_) {
                MaybePromoteToSelection(point);
            }
            if (selectionActive_) {
                UpdateSelectionRect(point);
            }
            return false;
        case WM_RBUTTONUP:
            if (!rightButtonDown_) {
                return false;
            }
            {
                const bool shouldConsume = selectionActive_;
                CompleteSelection(point);
                if (!shouldConsume) {
                    OpenDesktopContextMenu(point);
                }
                return true;
            }
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_MOUSEWHEEL:
            if (rightButtonDown_) {
                ResetDragState();
            }
            return false;
        default:
            return false;
    }
}

void MouseController::MaybePromoteToSelection(const POINT& currentPoint) {
    const int dx = std::abs(currentPoint.x - downPoint_.x);
    const int dy = std::abs(currentPoint.y - downPoint_.y);
    if (dx < kDragThresholdPixels && dy < kDragThresholdPixels) {
        return;
    }

    selectionActive_ = true;
    Infrastructure::Logger::Get().Info(L"[Selection] promoted to active mode.");
    if (onStarted_) {
        onStarted_(downPoint_);
    }
    UpdateSelectionRect(currentPoint);
}

void MouseController::UpdateSelectionRect(const POINT& currentPoint) {
    if (!selectionActive_ || !onUpdated_) {
        return;
    }
    onUpdated_(MakeNormalizedRect(downPoint_, currentPoint));
}

void MouseController::CompleteSelection(const POINT& releasePoint) {
    const bool active = selectionActive_;
    selectionActive_ = false;
    rightButtonDown_ = false;

    if (!active) {
        return;
    }

    const RECT selection = MakeNormalizedRect(downPoint_, releasePoint);
    const int width = selection.right - selection.left;
    const int height = selection.bottom - selection.top;
    if (width < kDragThresholdPixels || height < kDragThresholdPixels) {
        if (onCanceled_) {
            onCanceled_();
        }
        return;
    }

    if (onCompleted_) {
        onCompleted_(selection, releasePoint);
    }
}
}  // namespace Interaction
