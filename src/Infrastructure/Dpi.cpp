#include "Infrastructure/Dpi.h"

#include <windows.h>

namespace Infrastructure {
void EnablePerMonitorDpiAwareness() {
    using SetDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        const auto setContext = reinterpret_cast<SetDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext != nullptr &&
            setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }

    SetProcessDPIAware();
}
}  // namespace Infrastructure

