#include "Persistence/Models.h"

#include <windows.h>

#include <iomanip>
#include <sstream>

namespace Persistence {
std::wstring BuildIconIdentity(const Desktop::DesktopIcon& icon) {
    std::wstringstream stream;
    stream << L"name=";
    stream << (icon.displayName.empty() ? L"<empty>" : icon.displayName);
    stream << L"|index=" << icon.index;
    return stream.str();
}

std::wstring UtcNowIso8601() {
    SYSTEMTIME systemTime{};
    GetSystemTime(&systemTime);

    std::wstringstream stream;
    stream << std::setfill(L'0')
           << std::setw(4) << systemTime.wYear << L"-"
           << std::setw(2) << systemTime.wMonth << L"-"
           << std::setw(2) << systemTime.wDay << L"T"
           << std::setw(2) << systemTime.wHour << L":"
           << std::setw(2) << systemTime.wMinute << L":"
           << std::setw(2) << systemTime.wSecond << L"Z";
    return stream.str();
}
}  // namespace Persistence

