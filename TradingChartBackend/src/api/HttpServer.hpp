#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "api/Router.hpp"

namespace ttp::api {

class IoContext {
public:
    class WorkGuard {
    public:
        explicit WorkGuard(IoContext* /*context*/) noexcept {}
        WorkGuard(const WorkGuard&) = delete;
        WorkGuard& operator=(const WorkGuard&) = delete;
        WorkGuard(WorkGuard&& other) noexcept = default;
        WorkGuard& operator=(WorkGuard&& other) noexcept = default;
        ~WorkGuard() = default;
    };

    WorkGuard makeWorkGuard() noexcept { return WorkGuard(this); }
    void stop() noexcept {}
    bool running() const noexcept { return true; }
};

struct Endpoint {
    std::string address;
    std::uint16_t port;
};

class HttpServer {
public:
    struct CorsConfig {
        bool enabled{false};
        std::string origin;
    };

    HttpServer(IoContext& ioContext, Endpoint endpoint, std::size_t threadCount);
    ~HttpServer();

    void start();
    void stop();
    void wait();

    void setCorsConfig(CorsConfig config);

private:
    void workerLoop(std::size_t workerId);
    void handleClient(int clientFd);

    IoContext& ioContext_;
    Endpoint endpoint_;
    std::size_t threadCount_;
    std::optional<IoContext::WorkGuard> workGuard_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
    int serverFd_ = -1;
    Router router_{};
    CorsConfig corsConfig_{};
};

}  // namespace ttp::api
