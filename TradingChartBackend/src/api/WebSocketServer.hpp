#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "api/Controllers.hpp"

namespace ttp::api {

class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    static WebSocketServer& instance();

    bool handleClient(int clientFd, const std::string& rawRequest, const Request& request);

    void broadcast(const std::string& jsonMessage);

    void configureKeepAlive(std::chrono::milliseconds pingPeriod,
                            std::chrono::milliseconds pongTimeout);
    void configureBackpressure(std::size_t maxMessages,
                               std::size_t maxBytes,
                               std::chrono::milliseconds stallTimeout);

    struct SessionSnapshot {
        int fd{-1};
        bool active{false};
        std::chrono::steady_clock::time_point lastMessageAt{};
        std::chrono::steady_clock::time_point lastPongAt{};
        int consecutivePongMisses{0};
        std::uint64_t bytesInTotal{0};
        std::uint64_t bytesOutTotal{0};
        std::size_t pendingSendQueueSize{0};
        std::size_t pendingSendQueueBytes{0};
        bool waitingForPong{false};
    };

    std::vector<SessionSnapshot> getSessionSnapshots();

private:
    struct Session;
    friend struct Session;

    struct Session {
        Session(WebSocketServer& server, int fd);

        WebSocketServer& server;

        std::atomic<int> fd;
        std::mutex writeMutex;
        mutable std::mutex stateMutex;
        std::atomic<bool> active{true};
        std::atomic<bool> closing{false};
        std::atomic<std::int64_t> lastActivityMs{0};
        std::atomic<std::int64_t> lastPingMs{0};
        std::chrono::steady_clock::time_point lastMessageAt;
        std::chrono::steady_clock::time_point lastPongAt;
        std::chrono::steady_clock::time_point lastMessageLogAt;
        std::chrono::steady_clock::time_point lastPongLogAt;
        std::chrono::steady_clock::time_point lastPingSentAt;
        int consecutivePongMisses{0};
        bool waitingForPong{false};
        std::uint64_t bytesInTotal{0};
        std::uint64_t bytesOutTotal{0};
        std::deque<std::vector<std::uint8_t>> pendingSendQueue;  // Hook para cola de envío (Tarea 3)

        void recordIncomingFrame(std::size_t bytes, bool isPong);
        void recordOutgoingFrame(std::size_t bytes);
        void recordPingSent();
        bool updatePongTimeout(std::chrono::steady_clock::time_point now,
                               std::chrono::milliseconds timeout,
                               int& consecutiveMissesOut,
                               std::chrono::milliseconds& sinceLastPong);
        SessionSnapshot snapshot() const;
    };

    using SessionPtr = std::shared_ptr<Session>;

    bool performHandshake(int clientFd, const std::string& rawRequest, const Request& request, SessionPtr& session);
    bool sendTextFrame(const SessionPtr& session, const std::string& message);
    bool sendPingFrame(const SessionPtr& session);
    bool sendPongFrame(const SessionPtr& session, const std::vector<std::uint8_t>& payload);
    bool sendFrame(const SessionPtr& session, std::uint8_t opcode, const std::vector<std::uint8_t>& payload);
    void sessionLoop(const SessionPtr& session);
    void removeSession(const SessionPtr& session);
    bool closeWithReason(const SessionPtr& session,
                         std::uint16_t closeCode,
                         const std::string& reasonString,
                         const std::string& deadReasonTag);
    static void closeSessionSocket(const SessionPtr& session);

    static bool recvAll(int fd, void* buffer, std::size_t length);
    void keepAliveLoop();

    void recordMessageReceived_();
    void recordMessageSent_();
    void recordCloseReason_(const std::string& deadReasonTag);

    std::mutex sessionsMutex_;
    std::vector<SessionPtr> sessions_;
    std::atomic<bool> running_{true};
    std::thread keepAliveThread_;
    std::mutex keepAliveMutex_;
    std::condition_variable keepAliveCv_;
    std::atomic<std::int64_t> pingPeriodMs_{30000};
    std::atomic<std::int64_t> pongTimeoutMs_{75000};
    std::atomic<std::size_t> sendQueueMaxMessages_{500};
    std::atomic<std::size_t> sendQueueMaxBytes_{15728640};
    std::atomic<std::int64_t> stallTimeoutMs_{20000};

    struct Stats {
        std::uint64_t closePongTimeout{0};
        std::uint64_t closeBackpressure{0};
        std::uint64_t closeReadError{0};
        std::uint64_t closeWriteError{0};
        std::uint64_t messagesSent{0};
        std::uint64_t messagesReceived{0};
        std::chrono::steady_clock::time_point lastSummaryLog{};
    };

    mutable std::mutex statsMutex_;
    Stats stats_{};
};

void broadcast(const std::string& jsonMessage);

}  // namespace ttp::api
