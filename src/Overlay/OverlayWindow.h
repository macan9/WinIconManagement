#pragma once

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Overlay {
class OverlayWindow {
public:
    using SelectionConfirmCallback = std::function<void(bool confirmed)>;
    using ActiveFenceResizeCallback = std::function<void(const RECT& updatedRect)>;
    using ActiveFenceDeleteCallback = std::function<void()>;

    OverlayWindow();
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    [[nodiscard]] bool Initialize(HINSTANCE instance, HWND ownerWindow);
    void Destroy();

    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] HWND Handle() const;

    void Show();
    void Hide();
    void SetFixedMode(bool fixedMode);
    void SetFenceRects(const std::vector<RECT>& fenceRects);
    void SetFenceRect(const RECT& fenceRect);
    void SetFencePresentation(const std::vector<std::wstring>& fenceTitles, std::optional<size_t> activeFenceIndex);
    void SetVirtualDesktopRect(const RECT& virtualDesktopRect);
    void SetDesktopHostWindow(HWND desktopHostWindow);
    void SetSelectionRect(const RECT& selectionRect);
    void ClearSelectionRect();
    void ShowSelectionConfirm(const RECT& selectionRect, const POINT& anchorPoint);
    void HideSelectionConfirm();
    [[nodiscard]] bool IsSelectionConfirmVisible() const;
    [[nodiscard]] bool IsPointInSelectionConfirm(POINT screenPoint) const;
    [[nodiscard]] bool HandleSelectionConfirmClick(WPARAM message, POINT screenPoint);
    [[nodiscard]] bool HandleActiveFenceResizeMouse(WPARAM message, POINT screenPoint);
    void SetSelectionConfirmCallback(SelectionConfirmCallback onSelectionConfirm);
    void SetActiveFenceResizeCallback(ActiveFenceResizeCallback onActiveFenceResize);
    void SetActiveFenceDeleteCallback(ActiveFenceDeleteCallback onActiveFenceDelete);

private:
    enum class SelectionConfirmAction {
        None = 0,
        Confirm = 1,
        Cancel = 2,
    };

    enum class InteractionHitTarget {
        Transparent = 0,
        SelectionConfirm = 1,
        ActiveFenceResizeHandle = 2,
        ActiveFenceMoveArea = 3,
        ActiveFenceDeleteButton = 4,
    };

    enum class ActiveFenceDragMode {
        None = 0,
        Move = 1,
        Resize = 2,
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool RegisterClass() const;
    [[nodiscard]] bool CreateOverlayWindow();
    void ApplyRoundedRegion();
    void ApplyClickThroughStyle();
    void ApplyLayeredAttributes();
    void RefreshPresentation();
    [[nodiscard]] bool RefreshFixedLayeredWindow();
    void RenderFenceRects(HDC hdc) const;
    void RenderFenceRectsToLayeredBitmap(void* bits, int width, int height) const;
    void NormalizeFenceRects();
    void NormalizeFenceRect();
    void EnsureDesktopLayerZOrder() const;
    [[nodiscard]] RECT SelectionRectToLocalRect(const RECT& screenRect) const;
    [[nodiscard]] RECT ExpandRectForSelectionStroke(const RECT& rect) const;
    void InvalidateSelectionRectDelta(const RECT* previousSelectionRect);
    void Paint(HWND hwnd);
    void NormalizeSelectionRect();
    [[nodiscard]] RECT BuildConfirmRect() const;
    [[nodiscard]] RECT BuildConfirmButtonRect() const;
    [[nodiscard]] RECT BuildCancelButtonRect() const;
    void DrawConfirmUI(HDC hdc) const;
    void DrawActiveFenceResizeHandle(HDC hdc) const;
    void DrawActiveFenceDeleteButton(HDC hdc) const;
    [[nodiscard]] SelectionConfirmAction HitTestConfirmAction(POINT localPoint) const;
    void SetSelectionConfirmAction(SelectionConfirmAction action);
    [[nodiscard]] std::optional<RECT> GetActiveFenceRect() const;
    [[nodiscard]] RECT BuildActiveFenceResizeHandleRect(const RECT& activeFenceRect) const;
    [[nodiscard]] RECT BuildActiveFenceDeleteButtonRect(const RECT& activeFenceRect) const;
    [[nodiscard]] InteractionHitTarget HitTestInteractiveTarget(POINT screenPoint) const;
    void UpdateInteractionHoverState(POINT screenPoint);
    void BeginActiveFenceInteraction(POINT screenPoint, ActiveFenceDragMode dragMode);
    void UpdateActiveFenceInteraction(POINT screenPoint);
    void FinishActiveFenceInteraction(bool commitChanges);

    HINSTANCE instance_;
    HWND ownerWindow_;
    HWND window_;
    HWND desktopHostWindow_;
    RECT virtualDesktopRect_;
    std::vector<RECT> fenceRects_;
    std::vector<std::wstring> fenceTitles_;
    std::optional<size_t> activeFenceIndex_;
    RECT fenceRect_;
    RECT selectionRect_;
    bool hasSelectionRect_;
    bool selectionConfirmVisible_;
    POINT selectionConfirmAnchor_;
    RECT selectionConfirmRect_;
    SelectionConfirmAction selectionConfirmAction_;
    SelectionConfirmAction selectionConfirmHoverAction_;
    SelectionConfirmCallback onSelectionConfirm_;
    ActiveFenceResizeCallback onActiveFenceResize_;
    ActiveFenceDeleteCallback onActiveFenceDelete_;
    bool fixedMode_;
    bool visible_;
    bool paintLogged_;
    BYTE currentLayeredAlpha_;
    bool usingPerPixelLayeredMode_;
    InteractionHitTarget hoverHitTarget_;
    ActiveFenceDragMode activeFenceDragMode_;
    POINT activeFenceDragStartPoint_;
    RECT activeFenceDragStartRect_;
};
}  // namespace Overlay
