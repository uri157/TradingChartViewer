#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace adapters::api::ws {

class SessionSendQueue {
public:
    using Clock = std::chrono::steady_clock;

    struct Config {
        std::size_t maxMessages = 500;
        std::size_t maxBytes = 15 * 1024 * 1024;  // 15 MB
        std::chrono::milliseconds stallTimeout{20000};
    };

    struct Callbacks {
        std::function<void(const std::shared_ptr<const std::string>&)> startWrite;
        std::function<void()> closeForBackpressure;
    };

    SessionSendQueue(const Config& config, Callbacks callbacks);
    ~SessionSendQueue();

    SessionSendQueue(const SessionSendQueue&) = delete;
    SessionSendQueue& operator=(const SessionSendQueue&) = delete;

    void enqueue(const std::shared_ptr<const std::string>& payload);
    void onWriteComplete();
    void shutdown();

    std::size_t queuedMessages() const;
    std::size_t queuedBytes() const;

private:
    struct PendingMessage {
        std::shared_ptr<const std::string> payload;
        std::size_t bytes = 0;
    };

    void stallThreadLoop_();
    bool aboveThresholdLocked_() const;
    void updateStallTimerLocked_(const Clock::time_point& now);
    void logQueueLocked_(const char* reason, const Clock::time_point& now);
    void clearQueueLocked_();

    const Config config_;
    Callbacks callbacks_;

    mutable std::mutex mutex_;
    std::deque<PendingMessage> queue_;
    std::size_t queuedBytes_ = 0;
    bool writeInProgress_ = false;
    bool closed_ = false;

    // Stall timer state
    std::thread stallThread_;
    std::condition_variable stallCv_;
    bool stopThread_ = false;
    bool stallArmed_ = false;
    Clock::time_point stallDeadline_{};

    // Logging throttling
    Clock::time_point lastLogTime_{};
    std::size_t lastLoggedMessages_ = 0;
    std::size_t lastLoggedBytes_ = 0;
};

}  // namespace adapters::api::ws

