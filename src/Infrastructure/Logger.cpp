#include "Infrastructure/Logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>

#include "Infrastructure/Paths.h"

namespace Infrastructure {
namespace {
std::wstring CurrentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &nowTime);

    std::wstringstream stream;
    stream << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S");
    return stream.str();
}
}  // namespace

Logger& Logger::Get() {
    static Logger logger;
    return logger;
}

bool Logger::Initialize() {
    const auto logDirectory = GetUserWritableAppDirectory() / L"logs";
    std::error_code errorCode;
    std::filesystem::create_directories(logDirectory, errorCode);
    if (errorCode) {
        return false;
    }

    logPath_ = logDirectory / L"app.log";
    output_.open(logPath_, std::ios::out | std::ios::app);
    return output_.is_open();
}

void Logger::Info(std::wstring_view message) {
    Write(L"INFO", message);
}

void Logger::Error(std::wstring_view message) {
    Write(L"ERROR", message);
}

const std::filesystem::path& Logger::LogPath() const {
    return logPath_;
}

void Logger::Write(std::wstring_view level, std::wstring_view message) {
    std::scoped_lock lock(mutex_);
    if (!output_.is_open()) {
        return;
    }
    output_ << L"[" << CurrentTimestamp() << L"] [" << level << L"] " << message << L"\n";
    output_.flush();
}
}  // namespace Infrastructure

