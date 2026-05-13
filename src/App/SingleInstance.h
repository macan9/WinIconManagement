#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace App {
class SingleInstance {
public:
    explicit SingleInstance(std::wstring_view mutexName);
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool Acquire();

private:
    std::wstring mutexName_;
    HANDLE mutex_;
};
}  // namespace App
