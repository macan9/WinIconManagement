#include "App/SingleInstance.h"

namespace App {
SingleInstance::SingleInstance(std::wstring_view mutexName) : mutexName_(mutexName), mutex_(nullptr) {}

SingleInstance::~SingleInstance() {
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstance::Acquire() {
    mutex_ = CreateMutexW(nullptr, FALSE, mutexName_.c_str());
    if (mutex_ == nullptr) {
        return false;
    }
    return GetLastError() != ERROR_ALREADY_EXISTS;
}
}  // namespace App

