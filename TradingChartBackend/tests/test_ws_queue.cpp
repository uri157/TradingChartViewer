#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "adapters/api/ws/SessionSendQueue.hpp"

using adapters::api::ws::SessionSendQueue;

namespace {
using namespace std::chrono_literals;

bool waitForCondition(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = SessionSendQueue::Clock::now() + timeout;
    while (SessionSendQueue::Clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

}  // namespace

int main() {
    using adapters::api::ws::SessionSendQueue;

    {
        SessionSendQueue::Config config;
        config.maxMessages = 1;
        config.maxBytes = 1024;
        config.stallTimeout = std::chrono::milliseconds(80);

        std::atomic<int> closeCount{0};
        SessionSendQueue::Callbacks callbacks;
        callbacks.startWrite = [](const std::shared_ptr<const std::string>&) {
            // Simulate a stalled client by never calling onWriteComplete.
        };
        callbacks.closeForBackpressure = [&]() { closeCount.fetch_add(1); };

        SessionSendQueue queue(config, callbacks);

        queue.enqueue(std::make_shared<const std::string>("m1"));
        queue.enqueue(std::make_shared<const std::string>("m2"));
        queue.enqueue(std::make_shared<const std::string>("m3"));

        if (!waitForCondition([&]() { return closeCount.load() == 1; }, std::chrono::milliseconds(300))) {
            std::cerr << "Expected queue to trigger backpressure close (closeCount=" << closeCount.load() << ")\n";
            return 1;
        }

        queue.shutdown();
    }

    {
        SessionSendQueue::Config config;
        config.maxMessages = 1;
        config.maxBytes = 1024;
        config.stallTimeout = std::chrono::milliseconds(200);

        std::atomic<int> closeCount{0};
        std::atomic<int> writes{0};
        SessionSendQueue::Callbacks callbacks;
        SessionSendQueue* queuePtr = nullptr;
        callbacks.startWrite = [&](const std::shared_ptr<const std::string>&) {
            writes.fetch_add(1);
        };
        callbacks.closeForBackpressure = [&]() { closeCount.fetch_add(1); };

        SessionSendQueue queue(config, callbacks);
        queuePtr = &queue;

        queue.enqueue(std::make_shared<const std::string>("m1"));
        queue.enqueue(std::make_shared<const std::string>("m2"));
        queue.enqueue(std::make_shared<const std::string>("m3"));

        std::this_thread::sleep_for(50ms);
        queuePtr->onWriteComplete();
        queuePtr->onWriteComplete();
        queuePtr->onWriteComplete();

        std::this_thread::sleep_for(250ms);

        if (closeCount.load() != 0) {
            std::cerr << "Queue should not close when it drains below threshold\n";
            return 1;
        }
        if (writes.load() < 1) {
            std::cerr << "Expected at least one write to start\n";
            return 1;
        }

        queue.shutdown();
    }

    return 0;
}

