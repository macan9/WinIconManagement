#pragma once

#include <windows.h>

namespace Overlay {
class OverlayWindow {
public:
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
    void SetFenceRect(const RECT& fenceRect);
    void SetVirtualDesktopRect(const RECT& virtualDesktopRect);
    void SetDesktopHostWindow(HWND desktopHostWindow);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool RegisterClass() const;
    [[nodiscard]] bool CreateOverlayWindow();
    void ApplyRoundedRegion();
    void ApplyClickThroughStyle();
    void NormalizeFenceRect();
    void EnsureDesktopLayerZOrder() const;
    void Paint(HWND hwnd);

    HINSTANCE instance_;
    HWND ownerWindow_;
    HWND window_;
    HWND desktopHostWindow_;
    RECT virtualDesktopRect_;
    RECT fenceRect_;
    bool fixedMode_;
    bool visible_;
    bool paintLogged_;
};
}  // namespace Overlay
