#include "Desktop/DesktopBackgroundRenderer.h"

#include <CommCtrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "Infrastructure/Logger.h"

namespace {
constexpr COLORREF kFenceFillColor = RGB(214, 228, 255);
constexpr COLORREF kFenceBorderColor = RGB(120, 157, 235);
constexpr int kFenceBorderWidth = 2;
constexpr int kFenceCornerRadiusPixels = 10;

RECT FenceRectToLocalRect(const RECT& screenRect, const RECT& virtualDesktopRect) {
    return RECT{
        screenRect.left - virtualDesktopRect.left,
        screenRect.top - virtualDesktopRect.top,
        screenRect.right - virtualDesktopRect.left,
        screenRect.bottom - virtualDesktopRect.top};
}

RECT NormalizeRect(const RECT& rect) {
    RECT normalized = rect;
    if (normalized.left > normalized.right) {
        std::swap(normalized.left, normalized.right);
    }
    if (normalized.top > normalized.bottom) {
        std::swap(normalized.top, normalized.bottom);
    }
    return normalized;
}

bool IsSameProcessWindow(HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    return processId != 0 && processId == GetCurrentProcessId();
}
}

namespace Desktop {
DesktopBackgroundRenderer::DesktopBackgroundRenderer()
    : virtualDesktopRect_{0, 0, 0, 0},
      fenceRects_(),
      bitmap_(nullptr),
      bitmapSize_{0, 0},
      attachedListView_(nullptr) {}

DesktopBackgroundRenderer::~DesktopBackgroundRenderer() {
    Destroy();
}

void DesktopBackgroundRenderer::Destroy() {
    if (attachedListView_ != nullptr && IsWindow(attachedListView_)) {
        ClearFromListView(attachedListView_);
    }
    ReleaseBitmap();
    attachedListView_ = nullptr;
    virtualDesktopRect_ = RECT{0, 0, 0, 0};
    fenceRects_.clear();
}

void DesktopBackgroundRenderer::SetVirtualDesktopRect(const RECT& virtualDesktopRect) {
    virtualDesktopRect_ = virtualDesktopRect;
    RebuildBitmap();
}

void DesktopBackgroundRenderer::SetFenceRects(const std::vector<RECT>& fenceRects) {
    fenceRects_ = fenceRects;
    RebuildBitmap();
}

bool DesktopBackgroundRenderer::ApplyToListView(HWND listViewWindow) {
    if (listViewWindow == nullptr || !IsWindow(listViewWindow)) {
        Infrastructure::Logger::Get().Info(L"[DesktopBackground] skip apply: list-view unavailable.");
        return false;
    }

    if (!IsSameProcessWindow(listViewWindow)) {
        Infrastructure::Logger::Get().Error(
            L"[DesktopBackground] skip apply: cross-process ListView custom message is unsafe.");
        return false;
    }

    if (!EnsureBitmap()) {
        Infrastructure::Logger::Get().Error(L"[DesktopBackground] EnsureBitmap failed.");
        return false;
    }

    LVBKIMAGEW backgroundImage{};
    backgroundImage.ulFlags = LVBKIF_SOURCE_HBITMAP | LVBKIF_TYPE_WATERMARK;
    backgroundImage.hbm = bitmap_;
    backgroundImage.xOffsetPercent = 0;
    backgroundImage.yOffsetPercent = 0;

    const LRESULT setResult = SendMessageW(
            listViewWindow,
            LVM_SETBKIMAGEW,
            0,
            reinterpret_cast<LPARAM>(&backgroundImage));
    if (setResult == FALSE) {
        Infrastructure::Logger::Get().Error(
            L"[DesktopBackground] LVM_SETBKIMAGEW failed. lastError=" + std::to_wstring(GetLastError()));
        return false;
    }

    attachedListView_ = listViewWindow;
    bitmap_ = nullptr;
    bitmapSize_.cx = 0;
    bitmapSize_.cy = 0;
    InvalidateRect(listViewWindow, nullptr, TRUE);
    UpdateWindow(listViewWindow);
    Infrastructure::Logger::Get().Info(
        L"[DesktopBackground] applied background bitmap to list-view. fenceCount=" +
        std::to_wstring(fenceRects_.size()) +
        L"; result=" + std::to_wstring(static_cast<long long>(setResult)) +
        L"; hwnd=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(listViewWindow)));
    return true;
}

void DesktopBackgroundRenderer::ClearFromListView(HWND listViewWindow) {
    if (listViewWindow == nullptr || !IsWindow(listViewWindow)) {
        return;
    }

    if (!IsSameProcessWindow(listViewWindow)) {
        if (attachedListView_ == listViewWindow) {
            attachedListView_ = nullptr;
        }
        Infrastructure::Logger::Get().Info(
            L"[DesktopBackground] skip clear: cross-process ListView custom message is unsafe.");
        return;
    }

    LVBKIMAGEW backgroundImage{};
    backgroundImage.ulFlags = LVBKIF_SOURCE_NONE;
    backgroundImage.hbm = nullptr;
    SendMessageW(listViewWindow, LVM_SETBKIMAGEW, 0, reinterpret_cast<LPARAM>(&backgroundImage));
    InvalidateRect(listViewWindow, nullptr, TRUE);
    UpdateWindow(listViewWindow);
    if (attachedListView_ == listViewWindow) {
        attachedListView_ = nullptr;
    }
    Infrastructure::Logger::Get().Info(L"[DesktopBackground] cleared list-view background bitmap.");
}

bool DesktopBackgroundRenderer::EnsureBitmap() {
    if (bitmap_ != nullptr) {
        return true;
    }

    const int width = std::max(1L, virtualDesktopRect_.right - virtualDesktopRect_.left);
    const int height = std::max(1L, virtualDesktopRect_.bottom - virtualDesktopRect_.top);
    if (width <= 0 || height <= 0) {
        Infrastructure::Logger::Get().Error(L"[DesktopBackground] invalid virtual desktop size.");
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        Infrastructure::Logger::Get().Error(L"[DesktopBackground] GetDC(nullptr) failed.");
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixelData = nullptr;
    bitmap_ = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixelData, nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (bitmap_ == nullptr || pixelData == nullptr) {
        Infrastructure::Logger::Get().Error(
            L"[DesktopBackground] CreateDIBSection failed. lastError=" + std::to_wstring(GetLastError()));
        bitmap_ = nullptr;
        return false;
    }

    bitmapSize_.cx = width;
    bitmapSize_.cy = height;

    HDC memoryDc = CreateCompatibleDC(nullptr);
    if (memoryDc == nullptr) {
        Infrastructure::Logger::Get().Error(L"[DesktopBackground] CreateCompatibleDC failed.");
        ReleaseBitmap();
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap_);
    RECT clientRect{0, 0, width, height};
    HBRUSH clearBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memoryDc, &clientRect, clearBrush);
    DeleteObject(clearBrush);

    HBRUSH fillBrush = CreateSolidBrush(kFenceFillColor);
    HPEN borderPen = CreatePen(PS_SOLID, kFenceBorderWidth, kFenceBorderColor);
    HGDIOBJ oldBrush = SelectObject(memoryDc, fillBrush);
    HGDIOBJ oldPen = SelectObject(memoryDc, borderPen);

    for (const RECT& fenceRect : fenceRects_) {
        RECT localRect = NormalizeRect(FenceRectToLocalRect(fenceRect, virtualDesktopRect_));
        if (localRect.right <= localRect.left || localRect.bottom <= localRect.top) {
            continue;
        }

        RoundRect(
            memoryDc,
            localRect.left,
            localRect.top,
            localRect.right,
            localRect.bottom,
            kFenceCornerRadiusPixels * 2,
            kFenceCornerRadiusPixels * 2);
    }

    SelectObject(memoryDc, oldPen);
    SelectObject(memoryDc, oldBrush);
    SelectObject(memoryDc, oldBitmap);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);
    DeleteDC(memoryDc);
    Infrastructure::Logger::Get().Info(
        L"[DesktopBackground] bitmap rebuilt. size=" + std::to_wstring(width) +
        L"x" + std::to_wstring(height) +
        L"; fenceCount=" + std::to_wstring(fenceRects_.size()));

    return true;
}

void DesktopBackgroundRenderer::RebuildBitmap() {
    const HWND listViewWindow = attachedListView_;
    ReleaseBitmap();
    if (listViewWindow != nullptr && IsWindow(listViewWindow)) {
        (void)ApplyToListView(listViewWindow);
    }
}

void DesktopBackgroundRenderer::ReleaseBitmap() {
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    bitmapSize_.cx = 0;
    bitmapSize_.cy = 0;
}
}  // namespace Desktop
