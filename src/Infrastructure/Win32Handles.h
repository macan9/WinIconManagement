#pragma once

#include <windows.h>

#include <utility>

namespace Infrastructure {
template <typename T, typename Traits>
class UniqueHandle {
public:
    UniqueHandle() : value_(Traits::Invalid()) {}
    explicit UniqueHandle(T value) : value_(value) {}

    ~UniqueHandle() {
        Reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.Release()) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            value_ = other.Release();
        }
        return *this;
    }

    [[nodiscard]] bool IsValid() const {
        return value_ != Traits::Invalid();
    }

    [[nodiscard]] T Get() const {
        return value_;
    }

    [[nodiscard]] T Release() {
        T current = value_;
        value_ = Traits::Invalid();
        return current;
    }

    void Reset(T value = Traits::Invalid()) {
        if (value_ != Traits::Invalid()) {
            Traits::Close(value_);
        }
        value_ = value;
    }

private:
    T value_;
};

struct KernelHandleTraits {
    static HANDLE Invalid() {
        return nullptr;
    }

    static void Close(HANDLE handle) {
        CloseHandle(handle);
    }
};

struct IconHandleTraits {
    static HICON Invalid() {
        return nullptr;
    }

    static void Close(HICON icon) {
        DestroyIcon(icon);
    }
};

struct MenuHandleTraits {
    static HMENU Invalid() {
        return nullptr;
    }

    static void Close(HMENU menu) {
        DestroyMenu(menu);
    }
};

struct HookHandleTraits {
    static HHOOK Invalid() {
        return nullptr;
    }

    static void Close(HHOOK hook) {
        UnhookWindowsHookEx(hook);
    }
};

struct GdiObjectTraits {
    static HGDIOBJ Invalid() {
        return nullptr;
    }

    static void Close(HGDIOBJ object) {
        DeleteObject(object);
    }
};

using UniqueKernelHandle = UniqueHandle<HANDLE, KernelHandleTraits>;
using UniqueIcon = UniqueHandle<HICON, IconHandleTraits>;
using UniqueMenu = UniqueHandle<HMENU, MenuHandleTraits>;
using UniqueHook = UniqueHandle<HHOOK, HookHandleTraits>;
using UniqueGdiObject = UniqueHandle<HGDIOBJ, GdiObjectTraits>;
}  // namespace Infrastructure

