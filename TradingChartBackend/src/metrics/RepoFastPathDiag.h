#pragma once

#include <cstdint>
#include <cstdlib>
#include <string_view>

#if defined(REPO_FASTPATH_DIAG)
#include <chrono>
#include <optional>

#include "core/Diag.h"
#endif

namespace metrics {

struct RepoFastPathConfig {
    bool fastPathEnabled{true};
    bool diagEnabled{false};
};

inline bool isTrueValue(std::string_view value) noexcept {
    return value == "1" || value == "true" || value == "TRUE" || value == "on" || value == "ON";
}

inline bool isFalseValue(std::string_view value) noexcept {
    return value == "0" || value == "false" || value == "FALSE" || value == "off" || value == "OFF";
}

inline RepoFastPathConfig computeRepoFastPathConfig() noexcept {
    RepoFastPathConfig config;

    if (const char* env = std::getenv("TTP_REPO_FASTPATH")) {
        const std::string_view value(env);
        if (isTrueValue(value)) {
            config.fastPathEnabled = true;
#if defined(REPO_FASTPATH_DIAG)
            config.diagEnabled = true;
#endif
        }
        else if (isFalseValue(value)) {
            config.fastPathEnabled = false;
            config.diagEnabled = false;
        }
    }

#if !defined(REPO_FASTPATH_DIAG)
    config.diagEnabled = false;
#endif

    return config;
}

inline const RepoFastPathConfig& repoFastPathConfig() noexcept {
    static const RepoFastPathConfig config = computeRepoFastPathConfig();
    return config;
}

inline bool repoFastPathEnabled() noexcept { return repoFastPathConfig().fastPathEnabled; }

inline bool repoFastPathDiagEnabled() noexcept { return repoFastPathConfig().diagEnabled; }

#if defined(REPO_FASTPATH_DIAG)

class RepoFastPathTimer {
public:
    explicit RepoFastPathTimer(const char* tag) noexcept {
        if (repoFastPathDiagEnabled()) {
            timer_.emplace(core::diag::timer(tag));
        }
    }

private:
    std::optional<core::diag::ScopedTimer> timer_{};
};

class RepoFastPathLatencyTimer {
public:
    explicit RepoFastPathLatencyTimer(const char* name) noexcept {
        if (repoFastPathDiagEnabled()) {
            name_ = name;
            start_ = std::chrono::steady_clock::now();
        }
    }

    ~RepoFastPathLatencyTimer() {
        if (name_ != nullptr) {
            const auto end = std::chrono::steady_clock::now();
            const auto nanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
            core::diag::observe(name_, static_cast<std::uint64_t>(nanos));
        }
    }

    RepoFastPathLatencyTimer(const RepoFastPathLatencyTimer&) = delete;
    RepoFastPathLatencyTimer& operator=(const RepoFastPathLatencyTimer&) = delete;
    RepoFastPathLatencyTimer(RepoFastPathLatencyTimer&&) noexcept = default;
    RepoFastPathLatencyTimer& operator=(RepoFastPathLatencyTimer&&) noexcept = default;

private:
    const char* name_{nullptr};
    std::chrono::steady_clock::time_point start_{};
};

inline void repoFastPathIncr(const char* name, std::uint64_t value = 1) noexcept {
    if (repoFastPathDiagEnabled() && name != nullptr && value > 0) {
        core::diag::incr(name, value);
    }
}

inline void repoFastPathObserve(const char* name, std::uint64_t nanos) noexcept {
    if (repoFastPathDiagEnabled() && name != nullptr) {
        core::diag::observe(name, nanos);
    }
}

#else  // !REPO_FASTPATH_DIAG

class RepoFastPathTimer {
public:
    explicit RepoFastPathTimer(const char*) noexcept {}
};

class RepoFastPathLatencyTimer {
public:
    explicit RepoFastPathLatencyTimer(const char*) noexcept {}
};

inline void repoFastPathIncr(const char*, std::uint64_t = 1) noexcept {}

inline void repoFastPathObserve(const char*, std::uint64_t) noexcept {}

#endif  // REPO_FASTPATH_DIAG

}  // namespace metrics

