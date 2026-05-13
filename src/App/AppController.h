#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "Desktop/DesktopIconService.h"
#include "Desktop/DesktopWindowResolver.h"
#include "Persistence/Database.h"
#include "Persistence/SettingsRepository.h"
#include "Persistence/SnapshotRepository.h"
#include "Overlay/OverlayWindow.h"
#include "Tray/TrayIcon.h"

namespace App {
class AppController {
public:
    explicit AppController(HINSTANCE instance);
    ~AppController();

    bool Initialize();
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void HandleCommand(HWND hwnd, WORD commandId);
    bool RegisterWindowClass();
    bool CreateMainWindow();
    bool InitializeTray();
    void ShowMainWindow();
    bool CreateDiagnosticsTextControl();
    void LayoutDiagnosticsTextControl(int clientWidth, int clientHeight);
    void UpdateDiagnosticsTextControl();
    void PaintMainWindow(HWND hwnd);
    void LogDesktopResolveDiagnostics() const;
    void LogDesktopIconDiagnostics() const;
    std::wstring BuildDesktopResolveStatusText() const;
    void ResolveDesktopWindows(bool fromManualReconnect);
    void RefreshDesktopIconSnapshot();
    bool EnsureDesktopConnection();
    bool EnsureDesktopAndIconsReady();
    void CacheOriginalIconPositions();
    bool InitializePersistence();
    void PersistBasicSettings();
    void PersistIconSnapshot(const std::wstring& name, const std::wstring& source);
    bool MoveTestDesktopIcon();
    bool RestoreOriginalDesktopLayout();
    void UpdateWindowTitle();
    void UpdateOverlayWindow();
    void UpdateDpiMetrics(UINT dpi);
    void ApplyDpiFonts();
    int ScaleForDpi(int value) const;
    UINT GetWindowDpi() const;

    HINSTANCE instance_;
    HWND mainWindow_;
    HWND diagnosticsTextControl_;
    Tray::TrayIcon trayIcon_;
    Desktop::DesktopWindowResolver desktopResolver_;
    Desktop::DesktopIconService desktopIconService_;
    Desktop::DesktopResolveResult desktopResolveResult_;
    int desktopIconCount_;
    std::vector<Desktop::DesktopIcon> desktopIcons_;
    std::vector<Desktop::DesktopIcon> originalDesktopIcons_;
    std::wstring desktopIconReadStatus_;
    std::wstring lastGridMoveSummary_;
    Persistence::Database database_;
    Persistence::SettingsRepository settingsRepository_;
    Persistence::SnapshotRepository snapshotRepository_;
    Overlay::OverlayWindow overlayWindow_;
    bool persistenceReady_;
    UINT trayCallbackMessage_;
    UINT taskbarCreatedMessage_;
    UINT_PTR desktopHealthTimerId_;
    UINT desktopHealthIntervalMs_;
    UINT currentDpi_;
    HFONT uiFont_;
    HFONT titleFont_;
    bool isPinned_;
    bool isPaused_;
    bool isExiting_;
    bool isDesktopConnected_;
};
}  // namespace App
