#include "Desktop/DesktopWindowResolver.h"

#include <array>
#include <vector>

namespace {
constexpr UINT kProgmanSpawnWorkerMessage = 0x052C;
constexpr WPARAM kProgmanSpawnWorkerWParam = 0xD;
constexpr UINT kProgmanSpawnTimeoutMs = 1000;

struct ShellDefViewSearchContext {
    HWND shellDefViewWindow = nullptr;
    HWND parentWindow = nullptr;
    HWND workerWindowAfterParent = nullptr;
    std::vector<HWND> workerWindows;
};

std::wstring GetWindowClassName(HWND window) {
    if (window == nullptr) {
        return L"<null>";
    }

    std::array<wchar_t, 256> buffer{};
    const int length = GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
    if (length <= 0) {
        return L"<unknown>";
    }
    return std::wstring(buffer.data(), static_cast<size_t>(length));
}

bool IsWindowClass(HWND window, const wchar_t* className) {
    if (window == nullptr || className == nullptr) {
        return false;
    }
    return GetWindowClassName(window) == className;
}

bool HasShellDefViewChild(HWND parentWindow) {
    if (parentWindow == nullptr) {
        return false;
    }
    return FindWindowExW(parentWindow, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr;
}

void TrySpawnWorkerWindow(HWND progmanWindow) {
    if (progmanWindow == nullptr || !IsWindow(progmanWindow)) {
        return;
    }

    DWORD_PTR messageResult = 0;
    SendMessageTimeoutW(
        progmanWindow,
        kProgmanSpawnWorkerMessage,
        kProgmanSpawnWorkerWParam,
        0,
        SMTO_NORMAL | SMTO_ABORTIFHUNG,
        kProgmanSpawnTimeoutMs,
        &messageResult);
    SendMessageTimeoutW(
        progmanWindow,
        kProgmanSpawnWorkerMessage,
        kProgmanSpawnWorkerWParam,
        1,
        SMTO_NORMAL | SMTO_ABORTIFHUNG,
        kProgmanSpawnTimeoutMs,
        &messageResult);
}

BOOL CALLBACK EnumWindowsForShellDefView(HWND topLevelWindow, LPARAM lParam) {
    auto* context = reinterpret_cast<ShellDefViewSearchContext*>(lParam);

    if (IsWindowClass(topLevelWindow, L"WorkerW")) {
        context->workerWindows.push_back(topLevelWindow);
    }

    if (context->shellDefViewWindow == nullptr) {
        HWND shellDefView = FindWindowExW(topLevelWindow, nullptr, L"SHELLDLL_DefView", nullptr);
        if (shellDefView != nullptr) {
            context->shellDefViewWindow = shellDefView;
            context->parentWindow = topLevelWindow;
            context->workerWindowAfterParent = FindWindowExW(nullptr, topLevelWindow, L"WorkerW", nullptr);
        }
    }
    return TRUE;
}

HWND ChooseWorkerAfterDefView(const ShellDefViewSearchContext& context) {
    if (context.workerWindowAfterParent != nullptr && IsWindow(context.workerWindowAfterParent)) {
        return context.workerWindowAfterParent;
    }

    for (HWND workerWindow : context.workerWindows) {
        if (workerWindow == nullptr || !IsWindow(workerWindow)) {
            continue;
        }
        if (workerWindow == context.parentWindow) {
            continue;
        }
        if (HasShellDefViewChild(workerWindow)) {
            continue;
        }
        return workerWindow;
    }
    return nullptr;
}

void FillOverlayAnchor(Desktop::DesktopResolveResult* result) {
    if (result == nullptr) {
        return;
    }

    result->overlayAnchorWindow = nullptr;
    result->overlayAnchorClassName = L"<null>";
    result->overlayAnchorStrategy = L"none";

    if (result->workerWindowAfterDefView != nullptr && IsWindow(result->workerWindowAfterDefView)) {
        result->overlayAnchorWindow = result->workerWindowAfterDefView;
        result->overlayAnchorClassName = GetWindowClassName(result->overlayAnchorWindow);
        result->overlayAnchorStrategy = L"WorkerWAfterDefView";
        return;
    }

    if (result->workerWindow != nullptr && IsWindow(result->workerWindow)) {
        result->overlayAnchorWindow = result->workerWindow;
        result->overlayAnchorClassName = GetWindowClassName(result->overlayAnchorWindow);
        result->overlayAnchorStrategy = L"DefViewParent";
        return;
    }

    if (result->progmanWindow != nullptr && IsWindow(result->progmanWindow)) {
        result->overlayAnchorWindow = result->progmanWindow;
        result->overlayAnchorClassName = GetWindowClassName(result->overlayAnchorWindow);
        result->overlayAnchorStrategy = L"ProgmanFallback";
    }
}
}  // namespace

namespace Desktop {
DesktopResolveResult DesktopWindowResolver::Resolve() const {
    DesktopResolveResult result{};

    result.progmanWindow = FindWindowW(L"Progman", nullptr);
    if (result.progmanWindow == nullptr) {
        result.resolvePath = L"FindWindowW(Progman)";
        result.failureStep = L"FindWindowW(Progman)";
        result.failureCode = GetLastError();
        return result;
    }
    result.progmanClassName = GetWindowClassName(result.progmanWindow);

    // Ask Progman to ensure WorkerW split is available before scanning.
    TrySpawnWorkerWindow(result.progmanWindow);

    ShellDefViewSearchContext context{};
    EnumWindows(&EnumWindowsForShellDefView, reinterpret_cast<LPARAM>(&context));
    result.shellDefViewWindow = context.shellDefViewWindow;
    result.workerWindow = context.parentWindow;
    result.workerWindowAfterDefView = ChooseWorkerAfterDefView(context);
    result.workerClassName = GetWindowClassName(result.workerWindow);
    result.workerAfterDefViewClassName = GetWindowClassName(result.workerWindowAfterDefView);
    result.usedEnumWindowsFallback = true;
    result.resolvePath = L"EnumWindows -> DefViewParent";

    if (result.shellDefViewWindow == nullptr) {
        result.resolvePath += L" -> SHELLDLL_DefView not found";
        result.failureStep = L"Find SHELLDLL_DefView";
        result.failureCode = GetLastError();
        return result;
    }
    result.shellDefViewClassName = GetWindowClassName(result.shellDefViewWindow);
    result.resolvePath += (result.workerWindowAfterDefView != nullptr)
        ? L" -> WorkerW(after DefView)"
        : L" -> WorkerW(after DefView) not found";

    result.listViewWindow = FindWindowExW(result.shellDefViewWindow, nullptr, L"SysListView32", L"FolderView");
    result.resolvePath += L" -> SysListView32(FolderView)";
    if (result.listViewWindow == nullptr) {
        result.resolvePath = result.resolvePath + L" -> fallback SysListView32(*)";
        result.listViewWindow = FindWindowExW(result.shellDefViewWindow, nullptr, L"SysListView32", nullptr);
    }

    if (result.listViewWindow == nullptr) {
        result.resolvePath += L" -> not found";
        result.failureStep = L"Find SysListView32";
        result.failureCode = GetLastError();
        return result;
    }
    result.listViewClassName = GetWindowClassName(result.listViewWindow);

    DWORD processId = 0;
    GetWindowThreadProcessId(result.listViewWindow, &processId);
    result.explorerProcessId = processId;

    FillOverlayAnchor(&result);
    result.success = true;
    return result;
}

bool DesktopWindowResolver::IsWindowChainValid(const DesktopResolveResult& result) {
    return IsWindow(result.progmanWindow) && IsWindow(result.shellDefViewWindow) && IsWindow(result.listViewWindow);
}
}  // namespace Desktop
