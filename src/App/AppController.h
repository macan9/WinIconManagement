#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Desktop/DesktopIconService.h"
#include "Desktop/DesktopWindowResolver.h"
#include "Interaction/MouseController.h"
#include "Persistence/Database.h"
#include "Persistence/FenceRepository.h"
#include "Persistence/RestoreSessionRepository.h"
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
    struct ManagedFenceState {
        Persistence::FenceRecord record;
        std::vector<Persistence::FenceIconRecord> icons;
    };

    struct TemporarySelectionState {
        bool active = false;
        RECT rect{0, 0, 0, 0};
    };

    struct PendingFenceCreationState {
        RECT selectionRect{0, 0, 0, 0};
        POINT anchorPoint{0, 0};
    };

    enum class FenceEditHitTarget {
        None = 0,
        Move = 1,
        Resize = 2,
        Delete = 3,
    };

    enum class DesktopControlMode {
        ExplorerDriven = 0,
    };

    struct FenceEditState {
        bool active = false;
        FenceEditHitTarget target = FenceEditHitTarget::None;
        long long fenceId = 0;
        POINT anchorPoint{0, 0};
        RECT originalBounds{0, 0, 0, 0};
        RECT previewBounds{0, 0, 0, 0};
    };

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
    void LoadBasicSettings();
    void LoadRestoreSession();
    void PersistRuntimeRestoreSession(std::wstring_view reason);
    void PersistCleanShutdownRestoreSession(std::wstring_view exitMode, bool restoreNeededAfterExit);
    void PersistBasicSettings();
    void PersistIconSnapshot(const std::wstring& name, const std::wstring& source);
    void ReloadManagedFences();
    void LoadActiveFenceSetting();
    void PersistActiveFenceSetting();
    void ApplyExplorerDrivenRuntimeState(bool fromReconnect);
    [[nodiscard]] std::optional<long long> FindManagedFenceIdAtPoint(const POINT& point) const;
    [[nodiscard]] std::optional<size_t> FindManagedFenceIndexById(long long fenceId) const;
    void SetActiveFence(std::optional<long long> fenceId);
    [[nodiscard]] std::optional<ManagedFenceState> BuildSingleActiveFenceState() const;
    [[nodiscard]] std::vector<Desktop::DesktopIcon> BuildOriginalIconsFromManagedFences() const;
    [[nodiscard]] std::vector<Desktop::DesktopIcon> BuildManagedFenceLayoutTargets() const;
    bool RestoreManagedFenceLayout(bool refreshFenceStateAfterMove);
    void HandleSelectionStarted(const POINT& startPoint);
    void HandleSelectionUpdated(const RECT& selectionRect);
    void HandleSelectionCompleted(const RECT& selectionRect, const POINT& releasePoint);
    void HandleSelectionCanceled();
    void ApplySelectionStartedOnUiThread();
    void ApplySelectionUpdatedOnUiThread(const RECT& selectionRect);
    void ApplySelectionCompletedOnUiThread(const RECT& selectionRect, const POINT& releasePoint);
    void ApplySelectionCanceledOnUiThread();
    [[nodiscard]] bool IsOnMainUiThread() const;
    [[nodiscard]] bool ShouldStartSelectionAt(const POINT& point);
    bool HandleSelectionConfirmMouseFilter(WPARAM message, const POINT& point);
    bool HandleFenceEditMouse(WPARAM message, const POINT& point);
    void ConfirmSelectionRect(const RECT& selectionRect, const POINT& anchorPoint);
    void HandleSelectionConfirmDecision(bool confirmed);
    void CancelSelectionRect();
    void ApplyFenceFromSelectionRect(const RECT& selectionRect);
    bool RenameActiveFence(HWND ownerWindow);
    bool DeleteActiveFence();
    bool UpdateActiveFenceBounds(const RECT& bounds);
    bool ResizeActiveFence(int deltaWidth, int deltaHeight);
    [[nodiscard]] std::vector<Desktop::DesktopIcon> CollectIconsInRect(const RECT& selectionRect) const;
    [[nodiscard]] RECT BuildFenceRectFromSelection(const RECT& selectionRect) const;
    [[nodiscard]] std::vector<Desktop::DesktopIcon> BuildIconsForFenceLayout(
        const std::vector<Desktop::DesktopIcon>& selectedIcons,
        const RECT& fenceRect) const;
    [[nodiscard]] long long SaveFenceSelection(
        const RECT& fenceRect,
        const std::vector<Desktop::DesktopIcon>& originalIcons,
        const std::vector<Desktop::DesktopIcon>& movedIcons);
    [[nodiscard]] RECT BuildDefaultFenceRect() const;
    [[nodiscard]] RECT GetPrimaryOverlayFenceRect() const;
    [[nodiscard]] RECT BuildFenceDeleteButtonRect(const RECT& fenceRect) const;
    [[nodiscard]] RECT BuildFenceResizeHandleRect(const RECT& fenceRect) const;
    [[nodiscard]] FenceEditHitTarget HitTestFenceEditTarget(long long fenceId, const POINT& point) const;
    void BeginFenceEditDrag(long long fenceId, FenceEditHitTarget target, const POINT& point);
    void UpdateFenceEditDrag(const POINT& point);
    void EndFenceEditDrag(bool commitChanges);
    void ApplyFencePreviewBounds(long long fenceId, const RECT& bounds);
    bool MoveTestDesktopIcon();
    bool RestoreOriginalDesktopLayout(bool keepManagedFencesForNextLaunch);
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
    std::vector<ManagedFenceState> managedFences_;
    TemporarySelectionState temporarySelection_;
    std::optional<PendingFenceCreationState> pendingFenceCreation_;
    FenceEditState fenceEditState_;
    std::optional<long long> activeFenceId_;
    std::wstring desktopIconReadStatus_;
    std::wstring lastGridMoveSummary_;
    Persistence::Database database_;
    Persistence::FenceRepository fenceRepository_;
    Persistence::RestoreSessionRepository restoreSessionRepository_;
    Persistence::SettingsRepository settingsRepository_;
    Persistence::SnapshotRepository snapshotRepository_;
    Persistence::RestoreSessionRecord restoreSession_;
    Overlay::OverlayWindow overlayWindow_;
    Interaction::MouseController mouseController_;
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
    bool shouldRestoreManagedFences_;
    DesktopControlMode desktopControlMode_;
};
}  // namespace App
