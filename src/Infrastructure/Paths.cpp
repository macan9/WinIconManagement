#include "Infrastructure/Paths.h"

#include <shlobj_core.h>

namespace Infrastructure {
std::filesystem::path GetUserWritableAppDirectory() {
    PWSTR localAppData = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
    if (FAILED(hr) || localAppData == nullptr) {
        return std::filesystem::temp_directory_path() / L"WinIconManagement";
    }

    std::filesystem::path appPath = std::filesystem::path(localAppData) / L"WinIconManagement";
    CoTaskMemFree(localAppData);
    return appPath;
}
}  // namespace Infrastructure

