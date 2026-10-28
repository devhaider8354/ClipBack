// ClipBack - common definitions shared by every module.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace clipback {

using Microsoft::WRL::ComPtr;

// 100-nanosecond units are used for every timestamp in the pipeline (Media Foundation convention).
constexpr int64_t kTicksPerSecond = 10'000'000LL;

// Monotonic clock in 100ns units (QueryPerformanceCounter based; matches Windows.Graphics.Capture SystemRelativeTime).
inline int64_t Now100ns()
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    // Split to avoid overflow: (c / freq) * 1e7 + ((c % freq) * 1e7) / freq
    const int64_t sec = c.QuadPart / freq.QuadPart;
    const int64_t rem = c.QuadPart % freq.QuadPart;
    return sec * kTicksPerSecond + (rem * kTicksPerSecond) / freq.QuadPart;
}

inline int64_t QpcTo100ns(int64_t qpc)
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    const int64_t sec = qpc / freq.QuadPart;
    const int64_t rem = qpc % freq.QuadPart;
    return sec * kTicksPerSecond + (rem * kTicksPerSecond) / freq.QuadPart;
}

// Small RAII helper for COM initialisation on worker threads.
class ComInitializer {
public:
    explicit ComInitializer(DWORD model = COINIT_MULTITHREADED) { hr_ = CoInitializeEx(nullptr, model); }
    ~ComInitializer() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;
private:
    HRESULT hr_;
};

// RAII wrapper for a Win32 HANDLE.
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) : h_(h) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    UniqueHandle& operator=(UniqueHandle&& o) noexcept { if (this != &o) { reset(); h_ = o.h_; o.h_ = nullptr; } return *this; }
    void reset(HANDLE h = nullptr) { if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); h_ = h; }
    HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE h_ = nullptr;
};

} // namespace clipback
