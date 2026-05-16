#include "Persistence/Models.h"

#include <windows.h>

#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace {
std::wstring ToLowerCopy(std::wstring_view value) {
    std::wstring lowered(value);
    for (wchar_t& ch : lowered) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return lowered;
}
}

namespace Persistence {
std::wstring BuildIconIdentity(const Desktop::DesktopIcon& icon) {
    std::wstringstream stream;
    if (!icon.parsingPath.empty()) {
        stream << L"shell=" << ToLowerCopy(icon.parsingPath);
        return stream.str();
    }

    const std::wstring_view displayName =
        icon.displayName.empty() ? std::wstring_view(L"<empty>") : std::wstring_view(icon.displayName);
    stream << L"name=" << ToLowerCopy(displayName);
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

