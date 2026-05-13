#include "Desktop/DesktopWindowResolver.h"

#include <array>

namespace {
struct ShellDefViewSearchContext {
    HWND shellDefViewWindow = nullptr;
    HWND parentWindow = nullptr;
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

BOOL CALLBACK EnumWindowsForShellDefView(HWND topLevelWindow, LPARAM lParam) {
    auto* context = reinterpret_cast<ShellDefViewSearchContext*>(lParam);
    HWND shellDefView = FindWindowExW(topLevelWindow, nullptr, L"SHELLDLL_DefView", nullptr);
    if (shellDefView != nullptr) {
        context->shellDefViewWindow = shellDefView;
        context->parentWindow = topLevelWindow;
        return FALSE;
    }
    return TRUE;
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

    result.shellDefViewWindow = FindWindowExW(result.progmanWindow, nullptr, L"SHELLDLL_DefView", nullptr);
    result.workerWindow = result.progmanWindow;
    result.workerClassName = GetWindowClassName(result.workerWindow);
    result.resolvePath = L"Progman -> SHELLDLL_DefView";

    if (result.shellDefViewWindow == nullptr) {
        ShellDefViewSearchContext context{};
        EnumWindows(&EnumWindowsForShellDefView, reinterpret_cast<LPARAM>(&context));
        result.shellDefViewWindow = context.shellDefViewWindow;
        result.workerWindow = context.parentWindow;
        result.workerClassName = GetWindowClassName(result.workerWindow);
        result.usedEnumWindowsFallback = true;
        result.resolvePath = L"EnumWindows -> WorkerW -> SHELLDLL_DefView";
    }

    if (result.shellDefViewWindow == nullptr) {
        result.resolvePath += L" -> not found";
        result.failureStep = L"Find SHELLDLL_DefView";
        result.failureCode = GetLastError();
        return result;
    }
    result.shellDefViewClassName = GetWindowClassName(result.shellDefViewWindow);

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

    result.success = true;
    return result;
}

bool DesktopWindowResolver::IsWindowChainValid(const DesktopResolveResult& result) {
    return IsWindow(result.progmanWindow) && IsWindow(result.shellDefViewWindow) && IsWindow(result.listViewWindow);
}
}  // namespace Desktop
