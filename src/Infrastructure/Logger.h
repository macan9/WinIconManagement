#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace Infrastructure {
class Logger {
public:
    static Logger& Get();

    bool Initialize();
    void Info(std::wstring_view message);
    void Error(std::wstring_view message);
    const std::filesystem::path& LogPath() const;

private:
    Logger() = default;
    void Write(std::wstring_view level, std::wstring_view message);

    std::filesystem::path logPath_;
    std::wofstream output_;
    std::mutex mutex_;
};
}  // namespace Infrastructure

