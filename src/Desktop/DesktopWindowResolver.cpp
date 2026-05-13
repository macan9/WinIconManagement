#include "Desktop/DesktopWindowResolver.h"

namespace {
struct ShellDefViewSearchContext {
    HWND shellDefViewWindow = nullptr;
    HWND parentWindow = nullptr;
};

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
        result.failureStep = L"FindWindowW(Progman)";
        result.failureCode = GetLastError();
        return result;
    }

    result.shellDefViewWindow = FindWindowExW(result.progmanWindow, nullptr, L"SHELLDLL_DefView", nullptr);
    result.workerWindow = result.progmanWindow;

    if (result.shellDefViewWindow == nullptr) {
        ShellDefViewSearchContext context{};
        EnumWindows(&EnumWindowsForShellDefView, reinterpret_cast<LPARAM>(&context));
        result.shellDefViewWindow = context.shellDefViewWindow;
        result.workerWindow = context.parentWindow;
    }

    if (result.shellDefViewWindow == nullptr) {
        result.failureStep = L"Find SHELLDLL_DefView";
        result.failureCode = GetLastError();
        return result;
    }

    result.listViewWindow = FindWindowExW(result.shellDefViewWindow, nullptr, L"SysListView32", L"FolderView");
    if (result.listViewWindow == nullptr) {
        result.listViewWindow = FindWindowExW(result.shellDefViewWindow, nullptr, L"SysListView32", nullptr);
    }

    if (result.listViewWindow == nullptr) {
        result.failureStep = L"Find SysListView32";
        result.failureCode = GetLastError();
        return result;
    }

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

