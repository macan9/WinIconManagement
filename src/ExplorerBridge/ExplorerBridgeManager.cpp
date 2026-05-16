#include "ExplorerBridge/ExplorerBridgeManager.h"

#include <tlhelp32.h>

#include <cwctype>
#include <chrono>
#include <cstring>
#include <exception>
#include <sstream>

#include "Infrastructure/Logger.h"
#include "Infrastructure/Paths.h"

namespace {
constexpr wchar_t kBridgeWindowClassName[] = L"WinIconManagement.ExplorerBridge.MessageWindow";
constexpr ULONG_PTR kFenceRectsCopyDataId = 0x57494D46;  // WIMF

struct FenceRectsPayloadHeader {
    UINT32 version;
    UINT32 count;
};

std::wstring ToLower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

std::wstring ErrorToString(DWORD error) {
    return std::to_wstring(static_cast<unsigned long long>(error));
}
}

namespace ExplorerBridge {
ExplorerBridgeManager::ExplorerBridgeManager()
    : injectedProcessId_(0),
      injected_(false) {}

ExplorerBridgeManager::~ExplorerBridgeManager() {
    Shutdown();
}

bool ExplorerBridgeManager::Inject(DWORD explorerProcessId) {
    if (explorerProcessId == 0) {
        Infrastructure::Logger::Get().Error(L"[ExplorerBridge] inject skipped: explorer pid is 0.");
        return false;
    }

    if (injected_ && injectedProcessId_ == explorerProcessId) {
        Infrastructure::Logger::Get().Info(
            L"[ExplorerBridge] inject skipped: already injected into pid=" + std::to_wstring(explorerProcessId));
        return true;
    }

    const std::filesystem::path sourceDllPath = ResolveBridgeDllPath();
    if (!std::filesystem::exists(sourceDllPath)) {
        Infrastructure::Logger::Get().Error(L"[ExplorerBridge] dll not found: " + sourceDllPath.wstring());
        return false;
    }

    const std::filesystem::path dllPath = PrepareRuntimeBridgeDll(sourceDllPath);
    if (dllPath.empty()) {
        return false;
    }

    const std::wstring dllFileName = ToLower(dllPath.filename().wstring());
    if (IsAlreadyLoaded(explorerProcessId, dllFileName)) {
        injected_ = true;
        injectedProcessId_ = explorerProcessId;
        Infrastructure::Logger::Get().Info(
            L"[ExplorerBridge] dll already loaded in Explorer. pid=" + std::to_wstring(explorerProcessId));
        return true;
    }

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE,
        explorerProcessId);
    if (process == nullptr) {
        Infrastructure::Logger::Get().Error(
            L"[ExplorerBridge] OpenProcess failed. pid=" + std::to_wstring(explorerProcessId) +
            L"; error=" + ErrorToString(GetLastError()));
        return false;
    }

    const std::wstring dllPathString = dllPath.wstring();
    const SIZE_T byteCount = (dllPathString.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, byteCount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remotePath == nullptr) {
        Infrastructure::Logger::Get().Error(
            L"[ExplorerBridge] VirtualAllocEx failed. error=" + ErrorToString(GetLastError()));
        CloseHandle(process);
        return false;
    }

    bool success = false;
    if (!WriteProcessMemory(process, remotePath, dllPathString.c_str(), byteCount, nullptr)) {
        Infrastructure::Logger::Get().Error(
            L"[ExplorerBridge] WriteProcessMemory failed. error=" + ErrorToString(GetLastError()));
    } else {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto loadLibrary = kernel32 != nullptr
            ? reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"))
            : nullptr;
        if (loadLibrary == nullptr) {
            Infrastructure::Logger::Get().Error(
                L"[ExplorerBridge] GetProcAddress(LoadLibraryW) failed. error=" + ErrorToString(GetLastError()));
        } else {
            HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
            if (thread == nullptr) {
                Infrastructure::Logger::Get().Error(
                    L"[ExplorerBridge] CreateRemoteThread failed. error=" + ErrorToString(GetLastError()));
            } else {
                const DWORD waitResult = WaitForSingleObject(thread, 5000);
                DWORD remoteResult = 0;
                GetExitCodeThread(thread, &remoteResult);
                CloseHandle(thread);
                success = waitResult == WAIT_OBJECT_0 && remoteResult != 0;
                Infrastructure::Logger::Get().Info(
                    L"[ExplorerBridge] remote LoadLibrary completed. waitResult=" +
                    std::to_wstring(waitResult) +
                    L"; remoteResult=" + std::to_wstring(remoteResult));
            }
        }
    }

    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    CloseHandle(process);

    if (!success) {
        return false;
    }

    injected_ = true;
    injectedProcessId_ = explorerProcessId;
    Infrastructure::Logger::Get().Info(
        L"[ExplorerBridge] injected. pid=" + std::to_wstring(explorerProcessId) +
        L"; dll=" + dllPath.wstring());
    return true;
}

bool ExplorerBridgeManager::UpdateFenceRects(const std::vector<RECT>& fenceRects) {
    HWND bridgeWindow = nullptr;
    for (int attempt = 0; attempt < 10; ++attempt) {
        bridgeWindow = FindBridgeMessageWindow();
        if (bridgeWindow != nullptr) {
            break;
        }
        Sleep(50);
    }
    if (bridgeWindow == nullptr) {
        Infrastructure::Logger::Get().Info(L"[ExplorerBridge] update fence rects skipped: bridge window not found.");
        return false;
    }

    const size_t payloadBytes = sizeof(FenceRectsPayloadHeader) + sizeof(RECT) * fenceRects.size();
    std::vector<BYTE> payload(payloadBytes);
    auto* header = reinterpret_cast<FenceRectsPayloadHeader*>(payload.data());
    header->version = 1;
    header->count = static_cast<UINT32>(fenceRects.size());
    if (!fenceRects.empty()) {
        std::memcpy(payload.data() + sizeof(FenceRectsPayloadHeader), fenceRects.data(), sizeof(RECT) * fenceRects.size());
    }

    COPYDATASTRUCT copyData{};
    copyData.dwData = kFenceRectsCopyDataId;
    copyData.cbData = static_cast<DWORD>(payload.size());
    copyData.lpData = payload.data();

    const LRESULT result = SendMessageW(bridgeWindow, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copyData));
    Infrastructure::Logger::Get().Info(
        L"[ExplorerBridge] fence rects sent. count=" + std::to_wstring(fenceRects.size()) +
        L"; result=" + std::to_wstring(static_cast<long long>(result)));
    return result != FALSE;
}

void ExplorerBridgeManager::Shutdown() {
    if (!injected_) {
        return;
    }

    Infrastructure::Logger::Get().Info(
        L"[ExplorerBridge] shutdown requested. pid=" + std::to_wstring(injectedProcessId_) +
        L"; note=PoC bridge cleans up when Explorer unloads or restarts.");
    injected_ = false;
    injectedProcessId_ = 0;
}

bool ExplorerBridgeManager::IsInjected() const {
    return injected_;
}

DWORD ExplorerBridgeManager::InjectedProcessId() const {
    return injectedProcessId_;
}

std::filesystem::path ExplorerBridgeManager::ResolveBridgeDllPath() const {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::path(L"ExplorerBridge.dll");
    }

    std::filesystem::path exePath(modulePath);
    return exePath.parent_path() / L"ExplorerBridge.dll";
}

std::filesystem::path ExplorerBridgeManager::PrepareRuntimeBridgeDll(const std::filesystem::path& sourceDllPath) const {
    try {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path runtimeDirectory =
            Infrastructure::GetUserWritableAppDirectory() / L"ExplorerBridge";
        std::filesystem::create_directories(runtimeDirectory);

        const std::filesystem::path runtimeDllPath =
            runtimeDirectory /
            (L"ExplorerBridge-" +
             std::to_wstring(GetCurrentProcessId()) +
             L"-" +
             std::to_wstring(static_cast<long long>(now)) +
             L".dll");

        std::filesystem::copy_file(
            sourceDllPath,
            runtimeDllPath,
            std::filesystem::copy_options::overwrite_existing);
        Infrastructure::Logger::Get().Info(
            L"[ExplorerBridge] runtime dll prepared. source=" + sourceDllPath.wstring() +
            L"; runtime=" + runtimeDllPath.wstring());
        return runtimeDllPath;
    } catch (const std::filesystem::filesystem_error& error) {
        Infrastructure::Logger::Get().Error(
            L"[ExplorerBridge] prepare runtime dll failed. source=" + sourceDllPath.wstring() +
            L"; error=" + std::wstring(error.what(), error.what() + std::strlen(error.what())));
    } catch (const std::exception& error) {
        Infrastructure::Logger::Get().Error(
            L"[ExplorerBridge] prepare runtime dll failed. error=" +
            std::wstring(error.what(), error.what() + std::strlen(error.what())));
    }
    return {};
}

bool ExplorerBridgeManager::IsAlreadyLoaded(DWORD explorerProcessId, const std::wstring& dllFileName) const {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, explorerProcessId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Infrastructure::Logger::Get().Info(
            L"[ExplorerBridge] module snapshot unavailable. pid=" + std::to_wstring(explorerProcessId) +
            L"; error=" + ErrorToString(GetLastError()));
        return false;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (ToLower(entry.szModule) == dllFileName) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

HWND ExplorerBridgeManager::FindBridgeMessageWindow() const {
    return FindWindowW(kBridgeWindowClassName, L"WinIconManagement ExplorerBridge");
}
}  // namespace ExplorerBridge
