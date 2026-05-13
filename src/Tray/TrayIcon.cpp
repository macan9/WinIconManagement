#include "Tray/TrayIcon.h"

#include <shellapi.h>

#include "Infrastructure/Logger.h"
#include "Resource.h"

namespace {
constexpr UINT kDefaultTrayIconId = 1;
}

namespace Tray {
TrayIcon::TrayIcon()
    : instance_(nullptr),
      ownerWindow_(nullptr),
      callbackMessage_(0),
      iconId_(kDefaultTrayIconId),
      isVisible_(false),
      isPinned_(false),
      isPaused_(false),
      trayMenu_(nullptr),
      trayIcon_(nullptr),
      tooltip_(L"WinIconManagement") {}

TrayIcon::~TrayIcon() {
    Remove();
    if (trayMenu_ != nullptr) {
        DestroyMenu(trayMenu_);
        trayMenu_ = nullptr;
    }
    if (trayIcon_ != nullptr) {
        DestroyIcon(trayIcon_);
        trayIcon_ = nullptr;
    }
}

bool TrayIcon::Initialize(HINSTANCE instance, HWND ownerWindow, UINT callbackMessage) {
    instance_ = instance;
    ownerWindow_ = ownerWindow;
    callbackMessage_ = callbackMessage;
    return LoadResources();
}

bool TrayIcon::Show() {
    if (isVisible_) {
        return true;
    }

    NOTIFYICONDATAW notifyData{};
    notifyData.cbSize = sizeof(notifyData);
    notifyData.hWnd = ownerWindow_;
    notifyData.uID = iconId_;
    notifyData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    notifyData.uCallbackMessage = callbackMessage_;
    notifyData.hIcon = trayIcon_;
    wcsncpy_s(notifyData.szTip, tooltip_.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &notifyData)) {
        Infrastructure::Logger::Get().Error(L"Shell_NotifyIconW(NIM_ADD) failed.");
        return false;
    }

    isVisible_ = true;
    Infrastructure::Logger::Get().Info(L"Tray icon added.");
    return true;
}

void TrayIcon::Remove() {
    if (!isVisible_) {
        return;
    }

    NOTIFYICONDATAW notifyData{};
    notifyData.cbSize = sizeof(notifyData);
    notifyData.hWnd = ownerWindow_;
    notifyData.uID = iconId_;

    if (!Shell_NotifyIconW(NIM_DELETE, &notifyData)) {
        Infrastructure::Logger::Get().Error(L"Shell_NotifyIconW(NIM_DELETE) failed.");
    } else {
        Infrastructure::Logger::Get().Info(L"Tray icon removed.");
    }
    isVisible_ = false;
}

bool TrayIcon::HandleCallbackMessage(LPARAM lParam) {
    const UINT eventMessage = static_cast<UINT>(lParam);
    if (eventMessage == WM_CONTEXTMENU || eventMessage == WM_RBUTTONUP || eventMessage == WM_LBUTTONUP) {
        ShowContextMenu();
        return true;
    }
    return false;
}

bool TrayIcon::RecreateAfterExplorerRestart() {
    if (isVisible_) {
        Remove();
    }
    const bool shown = Show();
    if (shown) {
        Infrastructure::Logger::Get().Info(L"Tray icon recreated after Explorer restart.");
    }
    return shown;
}

void TrayIcon::SetPinned(bool pinned) {
    isPinned_ = pinned;
}

void TrayIcon::SetPaused(bool paused) {
    isPaused_ = paused;
}

bool TrayIcon::LoadResources() {
    if (trayMenu_ == nullptr) {
        trayMenu_ = LoadMenuW(instance_, MAKEINTRESOURCEW(IDR_TRAY_MENU));
    }
    if (trayMenu_ == nullptr) {
        Infrastructure::Logger::Get().Error(L"LoadMenuW(IDR_TRAY_MENU) failed.");
        return false;
    }

    if (trayIcon_ == nullptr) {
        trayIcon_ = LoadAppIcon();
    }
    if (trayIcon_ == nullptr) {
        Infrastructure::Logger::Get().Error(L"Failed to load tray icon.");
        return false;
    }
    return true;
}

void TrayIcon::ShowContextMenu() {
    if (trayMenu_ == nullptr) {
        return;
    }

    HMENU popupMenu = GetSubMenu(trayMenu_, 0);
    if (popupMenu == nullptr) {
        return;
    }

    UpdateMenuState();

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(ownerWindow_);
    TrackPopupMenu(
        popupMenu,
        TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
        cursor.x,
        cursor.y,
        0,
        ownerWindow_,
        nullptr);
    PostMessageW(ownerWindow_, WM_NULL, 0, 0);
}

void TrayIcon::UpdateMenuState() {
    HMENU popupMenu = GetSubMenu(trayMenu_, 0);
    if (popupMenu == nullptr) {
        return;
    }

    CheckMenuItem(
        popupMenu,
        IDM_TRAY_TOGGLE_PIN,
        MF_BYCOMMAND | (isPinned_ ? MF_CHECKED : MF_UNCHECKED));

    CheckMenuItem(
        popupMenu,
        IDM_TRAY_PAUSE,
        MF_BYCOMMAND | (isPaused_ ? MF_CHECKED : MF_UNCHECKED));
}

HICON TrayIcon::LoadAppIcon() const {
    HICON icon = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        0,
        0,
        LR_DEFAULTSIZE));
    if (icon != nullptr) {
        return icon;
    }
    return LoadIconW(nullptr, IDI_APPLICATION);
}
}  // namespace Tray

