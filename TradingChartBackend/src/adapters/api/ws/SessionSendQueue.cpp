#include "adapters/api/ws/SessionSendQueue.hpp"

#include <utility>

#include "logging/Log.h"

namespace adapters::api::ws {
namespace {
constexpr std::chrono::seconds kLogInterval{1};
}

SessionSendQueue::SessionSendQueue(const Config& config, Callbacks callbacks)
    : config_(config), callbacks_(std::move(callbacks)) {
    stallThread_ = std::thread([this]() { stallThreadLoop_(); });
}

SessionSendQueue::~SessionSendQueue() { shutdown(); }

void SessionSendQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopThread_) {
            return;
        }
        stopThread_ = true;
        stallCv_.notify_all();
    }
    if (stallThread_.joinable()) {
        stallThread_.join();
    }
}

void SessionSendQueue::enqueue(const std::shared_ptr<const std::string>& payload) {
    if (!payload) {
        return;
    }

    const auto now = Clock::now();
    std::shared_ptr<const std::string> toWrite;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return;
        }

        queue_.push_back(PendingMessage{payload, payload->size()});
        queuedBytes_ += payload->size();

        updateStallTimerLocked_(now);
        logQueueLocked_("enqueue", now);

        if (!writeInProgress_) {
            writeInProgress_ = true;
            toWrite = queue_.front().payload;
        }
    }

    if (toWrite && callbacks_.startWrite) {
        callbacks_.startWrite(toWrite);
    }
}

void SessionSendQueue::onWriteComplete() {
    std::shared_ptr<const std::string> next;
    const auto now = Clock::now();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            writeInProgress_ = false;
            updateStallTimerLocked_(now);
            logQueueLocked_("drain", now);
            return;
        }

        auto finished = queue_.front();
        queue_.pop_front();
        if (queuedBytes_ >= finished.bytes) {
            queuedBytes_ -= finished.bytes;
        } else {
            queuedBytes_ = 0;
        }

        if (!queue_.empty()) {
            next = queue_.front().payload;
            writeInProgress_ = true;
        } else {
            writeInProgress_ = false;
        }

        updateStallTimerLocked_(now);
        logQueueLocked_("drain", now);
    }

    if (next && callbacks_.startWrite) {
        callbacks_.startWrite(next);
    }
}

std::size_t SessionSendQueue::queuedMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

std::size_t SessionSendQueue::queuedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queuedBytes_;
}

void SessionSendQueue::stallThreadLoop_() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopThread_) {
        if (!stallArmed_) {
            stallCv_.wait(lock, [this]() { return stopThread_ || stallArmed_; });
            if (stopThread_) {
                break;
            }
            continue;
        }

        const auto deadline = stallDeadline_;
        const bool woke = stallCv_.wait_until(lock, deadline, [this]() { return stopThread_ || !stallArmed_; });
        if (stopThread_) {
            break;
        }
        if (woke) {
            continue;
        }

        if (!stallArmed_) {
            continue;
        }
        if (!aboveThresholdLocked_()) {
            stallArmed_ = false;
            continue;
        }

        stallArmed_ = false;
        closed_ = true;
        clearQueueLocked_();
        const auto now = Clock::now();
        logQueueLocked_("stall_timeout", now);
        auto closeCb = callbacks_.closeForBackpressure;
        lock.unlock();
        if (closeCb) {
            closeCb();
        }
        lock.lock();
    }
}

bool SessionSendQueue::aboveThresholdLocked_() const {
    if (config_.maxMessages > 0 && queue_.size() > config_.maxMessages) {
        return true;
    }
    if (config_.maxBytes > 0 && queuedBytes_ > config_.maxBytes) {
        return true;
    }
    return false;
}

void SessionSendQueue::updateStallTimerLocked_(const Clock::time_point& now) {
    const bool above = aboveThresholdLocked_();
    if (above) {
        if (!stallArmed_) {
            stallArmed_ = true;
            stallDeadline_ = now + config_.stallTimeout;
            stallCv_.notify_all();
        }
    } else {
        if (stallArmed_) {
            stallArmed_ = false;
            stallCv_.notify_all();
        }
    }
}

void SessionSendQueue::logQueueLocked_(const char* reason, const Clock::time_point& now) {
    if (!reason) {
        reason = "unknown";
    }

    if (now - lastLogTime_ < kLogInterval && queue_.size() == lastLoggedMessages_ && queuedBytes_ == lastLoggedBytes_) {
        return;
    }

    lastLogTime_ = now;
    lastLoggedMessages_ = queue_.size();
    lastLoggedBytes_ = queuedBytes_;

    LOG_DEBUG(::logging::LogCategory::NET,
              "ws_send_queue reason=%s queued_msgs=%zu queued_bytes=%zu write_in_progress=%d",
              reason,
              lastLoggedMessages_,
              lastLoggedBytes_,
              writeInProgress_ ? 1 : 0);
}

void SessionSendQueue::clearQueueLocked_() {
    queue_.clear();
    queuedBytes_ = 0;
    writeInProgress_ = false;
}

}  // namespace adapters::api::ws

