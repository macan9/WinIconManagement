#pragma once

#include <windows.h>

#include <string>

namespace Desktop {
struct DesktopResolveResult {
    bool success = false;
    bool usedEnumWindowsFallback = false;
    HWND progmanWindow = nullptr;
    HWND workerWindow = nullptr;
    HWND shellDefViewWindow = nullptr;
    HWND listViewWindow = nullptr;
    DWORD explorerProcessId = 0;
    std::wstring resolvePath;
    std::wstring progmanClassName;
    std::wstring workerClassName;
    std::wstring shellDefViewClassName;
    std::wstring listViewClassName;
    std::wstring failureStep;
    DWORD failureCode = 0;
};

class DesktopWindowResolver {
public:
    DesktopResolveResult Resolve() const;
    static bool IsWindowChainValid(const DesktopResolveResult& result);
};
}  // namespace Desktop
