#pragma once

#include <windows.h>

#include <vector>

namespace Desktop {
class DesktopBackgroundRenderer {
public:
    DesktopBackgroundRenderer();
    ~DesktopBackgroundRenderer();

    DesktopBackgroundRenderer(const DesktopBackgroundRenderer&) = delete;
    DesktopBackgroundRenderer& operator=(const DesktopBackgroundRenderer&) = delete;

    void Destroy();
    void SetVirtualDesktopRect(const RECT& virtualDesktopRect);
    void SetFenceRects(const std::vector<RECT>& fenceRects);
    [[nodiscard]] bool ApplyToListView(HWND listViewWindow);
    void ClearFromListView(HWND listViewWindow);

private:
    [[nodiscard]] bool EnsureBitmap();
    void RebuildBitmap();
    void ReleaseBitmap();

    RECT virtualDesktopRect_;
    std::vector<RECT> fenceRects_;
    HBITMAP bitmap_;
    SIZE bitmapSize_;
    HWND attachedListView_;
};
}  // namespace Desktop
