#pragma once

#include <windows.h>

#include <vector>

namespace Background {
class BackgroundWindow {
public:
    BackgroundWindow();
    ~BackgroundWindow();

    BackgroundWindow(const BackgroundWindow&) = delete;
    BackgroundWindow& operator=(const BackgroundWindow&) = delete;

    [[nodiscard]] bool Initialize(HINSTANCE instance, HWND ownerWindow);
    void Destroy();

    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] HWND Handle() const;

    void Show();
    void Hide();
    void SetVirtualDesktopRect(const RECT& virtualDesktopRect);
    void SetDesktopHostWindow(HWND desktopHostWindow);
    void SetFenceRects(const std::vector<RECT>& fenceRects);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool RegisterClass() const;
    [[nodiscard]] bool CreateBackgroundWindow();
    [[nodiscard]] bool EnsureWindowCreated();
    void DestroyWindowHandle();
    void EnsureDesktopLayerZOrder() const;
    void UpdateWindowBounds();
    void ApplyFenceRegion();
    void PaintNow();
    void RenderContent(HDC hdc, const RECT& clientRect, const RECT& paintRect) const;
    void Paint(HWND hwnd);

    HINSTANCE instance_;
    HWND ownerWindow_;
    HWND window_;
    HWND desktopHostWindow_;
    RECT virtualDesktopRect_;
    std::vector<RECT> fenceRects_;
    bool visible_;
    bool shouldBeVisible_;
    bool paintLogged_;
};
}  // namespace Background
