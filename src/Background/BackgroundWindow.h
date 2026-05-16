#pragma once

#include <windows.h>

#include <vector>

namespace Background {
enum class DesktopHostLayer {
    BehindExplorerIcons = 0,
    ShellDefViewBehindListView = 1,
    Fallback = 2,
};

class BackgroundWindow {
public:
    BackgroundWindow();
    ~BackgroundWindow();

    BackgroundWindow(const BackgroundWindow&) = delete;
    BackgroundWindow& operator=(const BackgroundWindow&) = delete;

    [[nodiscard]] bool Initialize(HINSTANCE instance, HWND ownerWindow);
    void Destroy();

    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] bool IsVisible() const;
    [[nodiscard]] HWND Handle() const;

    void Show();
    void Hide();
    void SetVirtualDesktopRect(const RECT& virtualDesktopRect);
    void SetDesktopHostWindow(HWND desktopHostWindow, DesktopHostLayer desktopHostLayer);
    void SetFenceRects(const std::vector<RECT>& fenceRects);

private:
    struct FenceVisualWindow {
        HWND window = nullptr;
        RECT screenRect{0, 0, 0, 0};
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool RegisterClass() const;
    [[nodiscard]] HWND CreateFenceWindow(const RECT& fenceRect);
    void DestroyFenceWindows();
    void RebuildFenceWindows();
    void UpdateFenceWindowBounds();
    void ShowFenceWindows();
    void HideFenceWindows();
    void EnsureDesktopLayerZOrder() const;
    void EnsureHostWindowReady() const;
    [[nodiscard]] bool IsDesktopHostUsable() const;
    [[nodiscard]] RECT GetEffectiveVirtualDesktopRect() const;
    [[nodiscard]] HWND GetFenceWindowZOrderTarget() const;
    void LogHostDiagnostics(const wchar_t* reason) const;
    void LogFenceWindowDiagnostics(HWND fenceWindow, const RECT& fenceRect) const;
    void PaintFenceWindow(HWND hwnd) const;
    [[nodiscard]] const FenceVisualWindow* FindFenceWindow(HWND hwnd) const;

    HINSTANCE instance_;
    HWND ownerWindow_;
    HWND desktopHostWindow_;
    DesktopHostLayer desktopHostLayer_;
    RECT virtualDesktopRect_;
    std::vector<RECT> fenceRects_;
    std::vector<FenceVisualWindow> fenceWindows_;
    bool visible_;
    bool shouldBeVisible_;
};
}  // namespace Background
