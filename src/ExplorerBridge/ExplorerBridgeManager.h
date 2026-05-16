#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ExplorerBridge {
class ExplorerBridgeManager {
public:
    ExplorerBridgeManager();
    ~ExplorerBridgeManager();

    ExplorerBridgeManager(const ExplorerBridgeManager&) = delete;
    ExplorerBridgeManager& operator=(const ExplorerBridgeManager&) = delete;

    bool Inject(DWORD explorerProcessId);
    bool UpdateFenceRects(const std::vector<RECT>& fenceRects);
    void Shutdown();
    [[nodiscard]] bool IsInjected() const;
    [[nodiscard]] DWORD InjectedProcessId() const;

private:
    [[nodiscard]] std::filesystem::path ResolveBridgeDllPath() const;
    [[nodiscard]] std::filesystem::path PrepareRuntimeBridgeDll(const std::filesystem::path& sourceDllPath) const;
    [[nodiscard]] bool IsAlreadyLoaded(DWORD explorerProcessId, const std::wstring& dllFileName) const;
    [[nodiscard]] HWND FindBridgeMessageWindow() const;

    DWORD injectedProcessId_;
    bool injected_;
};
}  // namespace ExplorerBridge
