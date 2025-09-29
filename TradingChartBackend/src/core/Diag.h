#pragma once

#include <cstdint>

namespace core::diag {

#ifdef TTP_ENABLE_DIAG

struct ScopedTimer {
    explicit ScopedTimer(const char* tag) noexcept;
    ~ScopedTimer();

    ScopedTimer(ScopedTimer&& other) noexcept;
    ScopedTimer& operator=(ScopedTimer&&) = delete;
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char* tag_{nullptr};
    std::uint64_t startNs_{0};
    bool active_{false};
};

ScopedTimer timer(const char* tag) noexcept;
void incr(const char* name, std::uint64_t v = 1) noexcept;
void observe(const char* name, std::uint64_t nanos) noexcept;
void diag_tick() noexcept;

#else  // TTP_ENABLE_DIAG

struct ScopedTimer {
    explicit ScopedTimer(const char*) noexcept {}
    ~ScopedTimer() = default;

    ScopedTimer(ScopedTimer&&) noexcept = default;
    ScopedTimer& operator=(ScopedTimer&&) noexcept = default;
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

inline ScopedTimer timer(const char*) noexcept { return ScopedTimer(nullptr); }
inline void incr(const char*, std::uint64_t = 1) noexcept {}
inline void observe(const char*, std::uint64_t) noexcept {}
inline void diag_tick() noexcept {}

#endif  // TTP_ENABLE_DIAG

}  // namespace core::diag

