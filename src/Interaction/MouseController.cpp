#include "Interaction/MouseController.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#include "Infrastructure/Logger.h"

namespace {
constexpr int kDragThresholdPixels = 12;
constexpr DWORD kSelectionLogThrottleMs = 500;

Interaction::MouseController* g_activeController = nullptr;

RECT MakeNormalizedRect(const POINT& a, const POINT& b) {
    RECT rect{};
    rect.left = std::min(a.x, b.x);
    rect.top = std::min(a.y, b.y);
    rect.right = std::max(a.x, b.x);
    rect.bottom = std::max(a.y, b.y);
    return rect;
}

std::wstring PointToString(const POINT& point) {
    std::wstringstream stream;
    stream << L"(" << point.x << L"," << point.y << L")";
    return stream.str();
}

std::wstring GetWindowClassName(HWND window) {
    if (window == nullptr) {
        return L"<null>";
    }

    wchar_t className[128]{};
    const int copied = GetClassNameW(window, className, static_cast<int>(std::size(className)));
    if (copied <= 0) {
        return L"<unknown>";
    }
    return std::wstring(className, copied);
}

bool IsDesktopShellClassName(const std::wstring& className) {
    return className == L"Progman" ||
           className == L"WorkerW" ||
           className == L"SHELLDLL_DefView" ||
           className == L"SysListView32";
}
}  // namespace

namespace Interaction {
MouseController::MouseController()
    : hook_(nullptr),
      desktopListViewWindow_(nullptr),
      enabled_(false),
      leftButtonDown_(false),
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
    if (enabled_ == enabled) {
        return;
    }

    enabled_ = enabled;
    Infrastructure::Logger::Get().Info(
        L"[Selection] mouse controller enabled=" + std::wstring(enabled_ ? L"true" : L"false"));
    if (enabled_) {
        if (!Start()) {
            enabled_ = false;
            Infrastructure::Logger::Get().Error(L"[Selection] failed to enable mouse controller hook.");
        }
    } else {
        ResetDragState();
        Stop();
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

void MouseController::SetSelectionStartFilterCallback(SelectionStartFilterCallback onFilter) {
    selectionStartFilter_ = std::move(onFilter);
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

    POINT clientPoint = screenPoint;
    if (ScreenToClient(desktopListViewWindow_, &clientPoint) == FALSE) {
        return false;
    }

    RECT clientRect{};
    const bool isPointInsideListView =
        GetClientRect(desktopListViewWindow_, &clientRect) != FALSE &&
        PtInRect(&clientRect, clientPoint) != FALSE;

    HWND underCursor = WindowFromPoint(screenPoint);
    if (underCursor == nullptr) {
        return false;
    }

    if (underCursor == desktopListViewWindow_ || IsChild(desktopListViewWindow_, underCursor) != FALSE) {
        return true;
    }

    HWND current = underCursor;
    while (current != nullptr) {
        if (current == desktopListViewWindow_ ||
            (isPointInsideListView && IsDesktopShellClassName(GetWindowClassName(current)))) {
            return true;
        }
        current = GetParent(current);
    }
    return false;
}

void MouseController::ClickDesktopBackground(POINT screenPoint) const {
    if (desktopListViewWindow_ == nullptr || !IsWindow(desktopListViewWindow_)) {
        return;
    }

    POINT clientPoint = screenPoint;
    if (ScreenToClient(desktopListViewWindow_, &clientPoint) == FALSE) {
        return;
    }

    const LPARAM position = MAKELPARAM(static_cast<SHORT>(clientPoint.x), static_cast<SHORT>(clientPoint.y));
    PostMessageW(desktopListViewWindow_, WM_LBUTTONDOWN, MK_LBUTTON, position);
    PostMessageW(desktopListViewWindow_, WM_LBUTTONUP, 0, position);
}

void MouseController::ResetDragState() {
    const bool hadSelection = selectionActive_;
    leftButtonDown_ = false;
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
        case WM_LBUTTONDOWN:
            if (!IsPointOnDesktop(point)) {
                static DWORD s_lastNotDesktopLogTick = 0;
                const DWORD now = GetTickCount();
                if (now - s_lastNotDesktopLogTick >= kSelectionLogThrottleMs) {
                    s_lastNotDesktopLogTick = now;
                    const HWND underCursor = WindowFromPoint(point);
                    Infrastructure::Logger::Get().Info(
                        L"[Selection] ignored left-button down: point is not on desktop. point=" +
                        PointToString(point) +
                        L"; hwnd=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(underCursor)) +
                        L"; class=" + GetWindowClassName(underCursor));
                }
                return false;
            }
            if (selectionStartFilter_ && !selectionStartFilter_(point)) {
                static DWORD s_lastFilteredLogTick = 0;
                const DWORD now = GetTickCount();
                if (now - s_lastFilteredLogTick >= kSelectionLogThrottleMs) {
                    s_lastFilteredLogTick = now;
                    Infrastructure::Logger::Get().Info(
                        L"[Selection] ignored left-button down: selection start filter rejected. point=" +
                        PointToString(point));
                }
                return false;
            }
            leftButtonDown_ = true;
            selectionActive_ = false;
            downPoint_ = point;
            lastPoint_ = point;
            return true;
        case WM_MOUSEMOVE:
            if (!leftButtonDown_) {
                return false;
            }
            lastPoint_ = point;
            if (!selectionActive_) {
                MaybePromoteToSelection(point);
            }
            if (selectionActive_) {
                UpdateSelectionRect(point);
            }
            // Swallow drag-move input once we own the gesture so Explorer does not
            // repaint desktop hover/selection visuals underneath our preview.
            return true;
        case WM_LBUTTONUP:
            if (!leftButtonDown_) {
                return false;
            }
            {
                const bool shouldApplyDesktopClick = !selectionActive_;
                CompleteSelection(point);
                if (shouldApplyDesktopClick) {
                    ClickDesktopBackground(point);
                }
                return true;
            }
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_MOUSEWHEEL:
            if (leftButtonDown_) {
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
    leftButtonDown_ = false;

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
