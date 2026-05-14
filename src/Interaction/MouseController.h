#pragma once

#include <windows.h>

#include <functional>

namespace Interaction {
class MouseController {
public:
    using MouseEventFilterCallback = std::function<bool(WPARAM, const POINT&)>;
    using SelectionStartFilterCallback = std::function<bool(const POINT&)>;
    using SelectionStartedCallback = std::function<void(const POINT&)>;
    using SelectionUpdatedCallback = std::function<void(const RECT&)>;
    using SelectionCompletedCallback = std::function<void(const RECT&, const POINT&)>;
    using SelectionCanceledCallback = std::function<void()>;

    MouseController();
    ~MouseController();

    MouseController(const MouseController&) = delete;
    MouseController& operator=(const MouseController&) = delete;

    [[nodiscard]] bool Start();
    void Stop();

    void SetEnabled(bool enabled);
    void SetDesktopListViewWindow(HWND listViewWindow);
    void SetCallbacks(
        SelectionStartedCallback onStarted,
        SelectionUpdatedCallback onUpdated,
        SelectionCompletedCallback onCompleted,
        SelectionCanceledCallback onCanceled);
    void SetMouseEventFilterCallback(MouseEventFilterCallback onFilter);
    void SetSelectionStartFilterCallback(SelectionStartFilterCallback onFilter);

private:
    static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool IsPointOnDesktop(POINT screenPoint) const;
    void ClickDesktopBackground(POINT screenPoint) const;
    void ResetDragState();
    [[nodiscard]] bool HandleMouseEvent(WPARAM wParam, const MSLLHOOKSTRUCT* data);
    void MaybePromoteToSelection(const POINT& currentPoint);
    void UpdateSelectionRect(const POINT& currentPoint);
    void CompleteSelection(const POINT& releasePoint);

    HHOOK hook_;
    HWND desktopListViewWindow_;
    bool enabled_;
    bool leftButtonDown_;
    bool selectionActive_;
    POINT downPoint_;
    POINT lastPoint_;

    SelectionStartedCallback onStarted_;
    SelectionUpdatedCallback onUpdated_;
    SelectionCompletedCallback onCompleted_;
    SelectionCanceledCallback onCanceled_;
    MouseEventFilterCallback onMouseEventFilter_;
    SelectionStartFilterCallback selectionStartFilter_;
};
}  // namespace Interaction
