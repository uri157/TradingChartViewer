#include "api/WebSocketServer.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/Log.hpp"
#include "common/Metrics.hpp"

namespace ttp::api {

namespace {

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::size_t kMaxFrameSize = 1 * 1024 * 1024;  // 1 MiB máximo aceptado por cuadro
constexpr std::chrono::seconds kInactivityTimeout{90};
constexpr std::chrono::minutes kLivenessLogInterval{1};
constexpr std::chrono::minutes kCloseSummaryInterval{5};
constexpr std::uint16_t kCloseCodeNormal = 1000;
constexpr std::uint16_t kCloseCodeGoingAway = 1001;
constexpr std::uint16_t kCloseCodeAbnormal = 1006;
constexpr std::uint16_t kCloseCodePolicyViolation = 1008;

const char* closeCodeLabel(std::uint16_t code) {
    switch (code) {
        case kCloseCodeNormal:
            return "normal";
        case kCloseCodeGoingAway:
            return "going_away";
        case kCloseCodePolicyViolation:
            return "policy_violation";
        case kCloseCodeAbnormal:
            return "abnormal";
        default:
            return "unknown";
    }
}

std::int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string trim(const std::string& input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(start, end - start);
}

std::string toLower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::unordered_map<std::string, std::string> parseHeaders(const std::string& rawRequest) {
    std::unordered_map<std::string, std::string> headers;
    std::istringstream stream(rawRequest);
    std::string line;
    // descartar primera línea
    std::getline(stream, line);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        const auto colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }
        auto key = toLower(trim(line.substr(0, colonPos)));
        auto value = trim(line.substr(colonPos + 1));
        headers.emplace(std::move(key), std::move(value));
    }
    return headers;
}

class Sha1 {
public:
    Sha1() { reset(); }

    void update(const std::uint8_t* data, std::size_t len) {
        std::size_t i = 0;
        if (bufferSize_ != 0) {
            while (i < len && bufferSize_ < 64) {
                buffer_[bufferSize_++] = data[i++];
            }
            if (bufferSize_ == 64) {
                processBlock(buffer_.data());
                bufferSize_ = 0;
            }
        }
        while (i + 63 < len) {
            processBlock(data + i);
            i += 64;
        }
        while (i < len) {
            buffer_[bufferSize_++] = data[i++];
        }
        totalBits_ += static_cast<std::uint64_t>(len) * 8U;
    }

    std::array<std::uint8_t, 20> finalize() {
        buffer_[bufferSize_++] = 0x80;
        if (bufferSize_ > 56) {
            while (bufferSize_ < 64) {
                buffer_[bufferSize_++] = 0;
            }
            processBlock(buffer_.data());
            bufferSize_ = 0;
        }
        while (bufferSize_ < 56) {
            buffer_[bufferSize_++] = 0;
        }
        for (int i = 7; i >= 0; --i) {
            buffer_[bufferSize_++] = static_cast<std::uint8_t>((totalBits_ >> (static_cast<std::uint64_t>(i) * 8U)) & 0xFFU);
        }
        processBlock(buffer_.data());

        std::array<std::uint8_t, 20> digest{};
        auto writeWord = [&digest](std::uint32_t word, std::size_t offset) {
            digest[offset + 0] = static_cast<std::uint8_t>((word >> 24) & 0xFFU);
            digest[offset + 1] = static_cast<std::uint8_t>((word >> 16) & 0xFFU);
            digest[offset + 2] = static_cast<std::uint8_t>((word >> 8) & 0xFFU);
            digest[offset + 3] = static_cast<std::uint8_t>(word & 0xFFU);
        };
        writeWord(h0_, 0);
        writeWord(h1_, 4);
        writeWord(h2_, 8);
        writeWord(h3_, 12);
        writeWord(h4_, 16);
        reset();
        return digest;
    }

private:
    static std::uint32_t leftRotate(std::uint32_t value, unsigned int bits) {
        return (value << bits) | (value >> (32U - bits));
    }

    void processBlock(const std::uint8_t* block) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = leftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h0_;
        std::uint32_t b = h1_;
        std::uint32_t c = h2_;
        std::uint32_t d = h3_;
        std::uint32_t e = h4_;

        for (int i = 0; i < 80; ++i) {
            std::uint32_t f;
            std::uint32_t k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }
            const std::uint32_t temp = leftRotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = leftRotate(b, 30);
            b = a;
            a = temp;
        }

        h0_ += a;
        h1_ += b;
        h2_ += c;
        h3_ += d;
        h4_ += e;
    }

    void reset() {
        h0_ = 0x67452301U;
        h1_ = 0xEFCDAB89U;
        h2_ = 0x98BADCFEU;
        h3_ = 0x10325476U;
        h4_ = 0xC3D2E1F0U;
        bufferSize_ = 0;
        totalBits_ = 0;
    }

    std::uint32_t h0_;
    std::uint32_t h1_;
    std::uint32_t h2_;
    std::uint32_t h3_;
    std::uint32_t h4_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferSize_ = 0;
    std::uint64_t totalBits_ = 0;
};

std::array<std::uint8_t, 20> sha1(const std::string& input) {
    Sha1 sha;
    sha.update(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
    return sha.finalize();
}

std::string base64Encode(const std::uint8_t* data, std::size_t len) {
    static constexpr char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((len + 2) / 3) * 4);

    for (std::size_t i = 0; i < len; i += 3) {
        const std::uint32_t octetA = data[i];
        const std::uint32_t octetB = (i + 1 < len) ? data[i + 1] : 0U;
        const std::uint32_t octetC = (i + 2 < len) ? data[i + 2] : 0U;

        const std::uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

        output.push_back(chars[(triple >> 18) & 0x3FU]);
        output.push_back(chars[(triple >> 12) & 0x3FU]);
        output.push_back(i + 1 < len ? chars[(triple >> 6) & 0x3FU] : '=');
        output.push_back(i + 2 < len ? chars[triple & 0x3FU] : '=');
    }

    return output;
}

std::string computeAcceptKey(const std::string& clientKey) {
    auto digest = sha1(clientKey + kWebSocketGuid);
    return base64Encode(digest.data(), digest.size());
}

void sendHttpError(int fd, int statusCode, const std::string& statusText, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << ' ' << statusText << "\r\n";
    response << "Content-Type: text/plain\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;

    const auto responseStr = response.str();
    const char* data = responseStr.data();
    std::size_t remaining = responseStr.size();
    while (remaining > 0) {
        const auto written = ::send(fd, data, remaining, 0);
        if (written <= 0) {
            break;
        }
        remaining -= static_cast<std::size_t>(written);
        data += written;
    }
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}

}  // namespace

WebSocketServer::Session::Session(WebSocketServer& server_, int fd_)
    : server(server_), fd(fd_) {
    const auto nowMs = steadyNowMs();
    lastActivityMs.store(nowMs, std::memory_order_relaxed);
    lastPingMs.store(nowMs, std::memory_order_relaxed);

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(stateMutex);
    lastMessageAt = now;
    lastPongAt = now;
    lastPingSentAt = now;
    lastMessageLogAt = now - kLivenessLogInterval;
    lastPongLogAt = now - kLivenessLogInterval;
}

void WebSocketServer::Session::recordIncomingFrame(std::size_t bytes, bool isPong) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(stateMutex);
    lastMessageAt = now;
    bytesInTotal += static_cast<std::uint64_t>(bytes);
    server.recordMessageReceived_();
    const int sessionFd = fd.load(std::memory_order_relaxed);

    if (now - lastMessageLogAt >= kLivenessLogInterval) {
        lastMessageLogAt = now;
        LOG_DEBUG("WS session(" << sessionFd << ") last_msg_at actualizado a "
                                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                                         lastMessageAt.time_since_epoch())
                                         .count()
                                  << "ms");
    }

    if (isPong) {
        lastPongAt = now;
        waitingForPong = false;
        consecutivePongMisses = 0;
        if (now - lastPongLogAt >= kLivenessLogInterval) {
            lastPongLogAt = now;
            LOG_DEBUG("WS session(" << sessionFd << ") last_pong_at actualizado a "
                                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                                             lastPongAt.time_since_epoch())
                                             .count()
                                      << "ms");
        }
    }
}

void WebSocketServer::Session::recordOutgoingFrame(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(stateMutex);
    bytesOutTotal += static_cast<std::uint64_t>(bytes);
    server.recordMessageSent_();
}

void WebSocketServer::Session::recordPingSent() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(stateMutex);
    waitingForPong = true;
    lastPingSentAt = now;
}

bool WebSocketServer::Session::updatePongTimeout(std::chrono::steady_clock::time_point now,
                                                 std::chrono::milliseconds timeout,
                                                 int& consecutiveMissesOut,
                                                 std::chrono::milliseconds& sinceLastPong) {
    std::lock_guard<std::mutex> lock(stateMutex);
    sinceLastPong = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPongAt);
    if (sinceLastPong > timeout) {
        ++consecutivePongMisses;
        consecutiveMissesOut = consecutivePongMisses;
        return true;
    }
    consecutivePongMisses = 0;
    consecutiveMissesOut = 0;
    return false;
}

WebSocketServer::SessionSnapshot WebSocketServer::Session::snapshot() const {
    SessionSnapshot snap;
    snap.fd = fd.load(std::memory_order_relaxed);
    snap.active = active.load(std::memory_order_relaxed);
    snap.consecutivePongMisses = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        snap.lastMessageAt = lastMessageAt;
        snap.lastPongAt = lastPongAt;
        snap.consecutivePongMisses = consecutivePongMisses;
        snap.bytesInTotal = bytesInTotal;
        snap.bytesOutTotal = bytesOutTotal;
        snap.pendingSendQueueSize = pendingSendQueue.size();
        std::size_t queuedBytes = 0;
        for (const auto& frame : pendingSendQueue) {
            queuedBytes += frame.size();
        }
        snap.pendingSendQueueBytes = queuedBytes;
        snap.waitingForPong = waitingForPong;
    }
    return snap;
}

WebSocketServer::WebSocketServer() {
    stats_.lastSummaryLog = std::chrono::steady_clock::now() - kCloseSummaryInterval;
    keepAliveThread_ = std::thread([this]() { keepAliveLoop(); });
}

WebSocketServer::~WebSocketServer() {
    running_.store(false);
    keepAliveCv_.notify_all();
    if (keepAliveThread_.joinable()) {
        keepAliveThread_.join();
    }
    std::vector<SessionPtr> sessionsCopy;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessionsCopy = sessions_;
        sessions_.clear();
    }
    for (const auto& session : sessionsCopy) {
        if (!session) {
            continue;
        }
        closeWithReason(session, kCloseCodeGoingAway, "server_shutdown", "server_shutdown");
    }
}

WebSocketServer& WebSocketServer::instance() {
    static WebSocketServer server;
    return server;
}

void WebSocketServer::configureKeepAlive(std::chrono::milliseconds pingPeriod,
                                         std::chrono::milliseconds pongTimeout) {
    const auto safePing = std::max<std::int64_t>(1, pingPeriod.count());
    const auto safePong = std::max<std::int64_t>(1, pongTimeout.count());
    pingPeriodMs_.store(safePing, std::memory_order_relaxed);
    pongTimeoutMs_.store(safePong, std::memory_order_relaxed);
    keepAliveCv_.notify_all();
    LOG_INFO("Configuración de keepalive actualizada: ping_period="
             << safePing << "ms pong_timeout=" << safePong << "ms");
}

void WebSocketServer::configureBackpressure(std::size_t maxMessages,
                                            std::size_t maxBytes,
                                            std::chrono::milliseconds stallTimeout) {
    sendQueueMaxMessages_.store(maxMessages, std::memory_order_relaxed);
    sendQueueMaxBytes_.store(maxBytes, std::memory_order_relaxed);
    const auto safeStall = std::max<std::int64_t>(1, stallTimeout.count());
    stallTimeoutMs_.store(safeStall, std::memory_order_relaxed);

    LOG_INFO("Configuración de backpressure actualizada: max_msgs="
             << maxMessages << " max_bytes=" << maxBytes
             << " stall_timeout=" << safeStall << "ms");
}

bool WebSocketServer::handleClient(int clientFd, const std::string& rawRequest, const Request& request) {
    SessionPtr session;
    const bool targeted = performHandshake(clientFd, rawRequest, request, session);
    if (!targeted) {
        return false;
    }

    if (!session) {
        return true;  // solicitud a /ws pero handshake rechazado
    }

    std::size_t activeSessions = 0;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.push_back(session);
        activeSessions = sessions_.size();
    }

    const auto sessionFd = session->fd.load(std::memory_order_relaxed);
    const auto configuredPing = pingPeriodMs_.load(std::memory_order_relaxed);
    const auto configuredPong = pongTimeoutMs_.load(std::memory_order_relaxed);
    LOG_INFO("WS session(" << sessionFd
                             << ") ping scheduler iniciado: ping_period=" << configuredPing
                             << "ms pong_timeout=" << configuredPong << "ms");

    if (!sendTextFrame(session, R"({"event":"welcome"})")) {
        closeWithReason(session, kCloseCodeAbnormal, "write_error", "write_error");
        removeSession(session);
        return true;
    }

    std::thread([this, session]() { sessionLoop(session); }).detach();

    LOG_INFO("Cliente WebSocket conectado (" << activeSessions << " sesiones activas)");

    return true;
}

bool WebSocketServer::performHandshake(int clientFd,
                                       const std::string& rawRequest,
                                       const Request& request,
                                       SessionPtr& session) {
    if (!running_.load()) {
        return false;
    }

    if (request.method != "GET" || request.path != "/ws") {
        return false;
    }

    const auto headers = parseHeaders(rawRequest);
    const auto upgradeIt = headers.find("upgrade");
    if (upgradeIt == headers.end() || toLower(upgradeIt->second) != "websocket") {
        sendHttpError(clientFd, 400, "Bad Request", "Missing or invalid Upgrade header\n");
        return true;
    }

    const auto connectionIt = headers.find("connection");
    if (connectionIt == headers.end()) {
        sendHttpError(clientFd, 400, "Bad Request", "Missing Connection header\n");
        return true;
    }
    const auto connectionValue = toLower(connectionIt->second);
    if (connectionValue.find("upgrade") == std::string::npos) {
        sendHttpError(clientFd, 400, "Bad Request", "Connection header must include 'Upgrade'\n");
        return true;
    }

    const auto keyIt = headers.find("sec-websocket-key");
    if (keyIt == headers.end() || keyIt->second.empty()) {
        sendHttpError(clientFd, 400, "Bad Request", "Missing Sec-WebSocket-Key header\n");
        return true;
    }

    const auto acceptKey = computeAcceptKey(keyIt->second);

    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n";
    response << "Upgrade: websocket\r\n";
    response << "Connection: Upgrade\r\n";
    response << "Sec-WebSocket-Accept: " << acceptKey << "\r\n\r\n";

    const auto responseStr = response.str();
    const char* data = responseStr.data();
    std::size_t remaining = responseStr.size();
    while (remaining > 0) {
        const auto written = ::send(clientFd, data, remaining, 0);
        if (written <= 0) {
            ::shutdown(clientFd, SHUT_RDWR);
            ::close(clientFd);
            return true;
        }
        remaining -= static_cast<std::size_t>(written);
        data += written;
    }

    session = std::make_shared<Session>(*this, clientFd);
    return true;
}

bool WebSocketServer::sendTextFrame(const SessionPtr& session, const std::string& message) {
    std::vector<std::uint8_t> payload(message.begin(), message.end());
    return sendFrame(session, 0x1, payload);
}

bool WebSocketServer::sendPingFrame(const SessionPtr& session) {
    static const std::vector<std::uint8_t> emptyPayload;
    return sendFrame(session, 0x9, emptyPayload);
}

bool WebSocketServer::sendPongFrame(const SessionPtr& session, const std::vector<std::uint8_t>& payload) {
    return sendFrame(session, 0xA, payload);
}

bool WebSocketServer::sendFrame(const SessionPtr& session,
                                std::uint8_t opcode,
                                const std::vector<std::uint8_t>& payload) {
    if (!session || !session->active.load()) {
        return false;
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(2 + payload.size());
    frame.push_back(static_cast<std::uint8_t>(0x80U | (opcode & 0x0FU)));

    const std::uint64_t payloadSize = payload.size();
    if (payloadSize <= 125U) {
        frame.push_back(static_cast<std::uint8_t>(payloadSize & 0x7FU));
    } else if (payloadSize <= 0xFFFFU) {
        frame.push_back(126U);
        frame.push_back(static_cast<std::uint8_t>((payloadSize >> 8) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(payloadSize & 0xFFU));
    } else {
        frame.push_back(127U);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<std::uint8_t>((payloadSize >> shift) & 0xFFU));
        }
    }

    frame.insert(frame.end(), payload.begin(), payload.end());

    const std::size_t frameSize = frame.size();
    {
        std::lock_guard<std::mutex> lock(session->writeMutex);
        const int fd = session->fd.load(std::memory_order_relaxed);
        if (fd < 0) {
            return false;
        }

        const char* data = reinterpret_cast<const char*>(frame.data());
        std::size_t remaining = frame.size();
        while (remaining > 0) {
            const auto written = ::send(fd, data, remaining, 0);
            if (written <= 0) {
                return false;
            }
            remaining -= static_cast<std::size_t>(written);
            data += written;
        }
    }

    session->recordOutgoingFrame(frameSize);
    return true;
}

void WebSocketServer::sessionLoop(const SessionPtr& session) {
    enum class LoopExitReason { None, ClientClose, ReadError, WriteError };
    LoopExitReason exitReason = LoopExitReason::None;
    std::array<std::uint8_t, 2> header{};
    while (running_.load() && session->active.load()) {
        const int fd = session->fd.load(std::memory_order_relaxed);
        if (fd < 0) {
            break;
        }
        if (!recvAll(fd, header.data(), header.size())) {
            exitReason = LoopExitReason::ReadError;
            break;
        }

        const bool fin = (header[0] & 0x80U) != 0;
        const std::uint8_t opcode = static_cast<std::uint8_t>(header[0] & 0x0FU);
        const bool masked = (header[1] & 0x80U) != 0;
        std::uint64_t payloadLen = static_cast<std::uint64_t>(header[1] & 0x7FU);
        std::size_t frameBytes = header.size();

        if (!masked) {
            LOG_WARN("WebSocket frame sin máscara recibido, cerrando sesión");
            exitReason = LoopExitReason::ReadError;
            break;
        }

        if (payloadLen == 126U) {
            std::array<std::uint8_t, 2> extended{};
            if (!recvAll(fd, extended.data(), extended.size())) {
                exitReason = LoopExitReason::ReadError;
                break;
            }
            payloadLen = (static_cast<std::uint64_t>(extended[0]) << 8U) |
                         static_cast<std::uint64_t>(extended[1]);
            frameBytes += extended.size();
        } else if (payloadLen == 127U) {
            std::array<std::uint8_t, 8> extended{};
            if (!recvAll(fd, extended.data(), extended.size())) {
                exitReason = LoopExitReason::ReadError;
                break;
            }
            payloadLen = 0;
            for (std::uint8_t byte : extended) {
                payloadLen = (payloadLen << 8U) | static_cast<std::uint64_t>(byte);
            }
            frameBytes += extended.size();
        }

        if (payloadLen > kMaxFrameSize) {
            LOG_WARN("WebSocket frame demasiado grande (" << payloadLen << " bytes), cerrando sesión");
            exitReason = LoopExitReason::ReadError;
            break;
        }

        std::array<std::uint8_t, 4> mask{};
        if (!recvAll(fd, mask.data(), mask.size())) {
            exitReason = LoopExitReason::ReadError;
            break;
        }
        frameBytes += mask.size();

        std::vector<std::uint8_t> payload(payloadLen);
        if (payloadLen > 0 && !recvAll(fd, payload.data(), payload.size())) {
            exitReason = LoopExitReason::ReadError;
            break;
        }
        frameBytes += static_cast<std::size_t>(payloadLen);

        for (std::uint64_t i = 0; i < payloadLen; ++i) {
            payload[i] ^= mask[static_cast<std::size_t>(i % 4U)];
        }

        const auto now = steadyNowMs();
        session->lastActivityMs.store(now, std::memory_order_relaxed);

        session->recordIncomingFrame(frameBytes, opcode == 0xAU);

        if (opcode == 0x8U) {  // close
            exitReason = LoopExitReason::ClientClose;
            break;
        }
        if (opcode == 0x9U) {  // ping -> responder pong
            if (!sendPongFrame(session, payload)) {
                exitReason = LoopExitReason::WriteError;
                break;
            }
            session->lastActivityMs.store(now, std::memory_order_relaxed);
            continue;
        }
        if (opcode == 0xAU) {  // pong -> ignorar
            session->lastActivityMs.store(now, std::memory_order_relaxed);
            continue;
        }
        if (!fin && opcode == 0x0U) {
            // ignorar fragmentos continuados
            continue;
        }
        // ignorar texto/binario por ahora
    }

    std::string reasonTag;
    std::uint16_t closeCode = kCloseCodeGoingAway;
    switch (exitReason) {
        case LoopExitReason::ClientClose:
            reasonTag = "client_close";
            closeCode = kCloseCodeNormal;
            break;
        case LoopExitReason::ReadError:
            reasonTag = "read_error";
            closeCode = kCloseCodeAbnormal;
            break;
        case LoopExitReason::WriteError:
            reasonTag = "write_error";
            closeCode = kCloseCodeAbnormal;
            break;
        case LoopExitReason::None:
        default:
            reasonTag = "server_shutdown";
            closeCode = kCloseCodeGoingAway;
            break;
    }

    closeWithReason(session, closeCode, reasonTag, reasonTag);
    removeSession(session);
}

void WebSocketServer::removeSession(const SessionPtr& session) {
    std::size_t remaining = 0;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.erase(std::remove_if(sessions_.begin(),
                                       sessions_.end(),
                                       [&session](const SessionPtr& other) { return other.get() == session.get(); }),
                        sessions_.end());
        remaining = sessions_.size();
    }
    LOG_INFO("Cliente WebSocket desconectado (" << remaining << " sesiones activas)");
}

bool WebSocketServer::closeWithReason(const SessionPtr& session,
                                      std::uint16_t closeCode,
                                      const std::string& reasonString,
                                      const std::string& deadReasonTag) {
    if (!session) {
        return false;
    }

    bool expected = false;
    if (!session->closing.compare_exchange_strong(expected, true)) {
        return false;
    }

    std::string closeReason = reasonString;
    if (closeReason.size() > 123U) {
        closeReason = closeReason.substr(0, 123U);
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(2U + closeReason.size());
    payload.push_back(static_cast<std::uint8_t>((closeCode >> 8U) & 0xFFU));
    payload.push_back(static_cast<std::uint8_t>(closeCode & 0xFFU));
    payload.insert(payload.end(), closeReason.begin(), closeReason.end());

    (void)sendFrame(session, 0x8U, payload);

    {
        std::lock_guard<std::mutex> lock(session->stateMutex);
        session->waitingForPong = false;
    }

    session->active.store(false, std::memory_order_relaxed);
    closeSessionSocket(session);

    const auto snapshot = session->snapshot();
    const std::string deadTag = deadReasonTag.empty() ? std::string{"unknown"} : deadReasonTag;

    LOG_INFO("ws_session_close client_id="
             << snapshot.fd << " dead_reason=" << deadTag << " close_code=" << closeCodeLabel(closeCode)
             << " queue_msgs=" << snapshot.pendingSendQueueSize
             << " queue_bytes=" << snapshot.pendingSendQueueBytes
             << " consecutive_pong_misses=" << snapshot.consecutivePongMisses
             << " bytes_in_total=" << snapshot.bytesInTotal
             << " bytes_out_total=" << snapshot.bytesOutTotal);

    recordCloseReason_(deadTag);

    return true;
}

void WebSocketServer::closeSessionSocket(const SessionPtr& session) {
    if (!session) {
        return;
    }
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(session->writeMutex);
        const int currentFd = session->fd.load(std::memory_order_relaxed);
        if (currentFd >= 0) {
            fd = currentFd;
            session->fd.store(-1, std::memory_order_relaxed);
        }
    }
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

bool WebSocketServer::recvAll(int fd, void* buffer, std::size_t length) {
    if (fd < 0) {
        return false;
    }
    std::size_t received = 0;
    auto* data = static_cast<std::uint8_t*>(buffer);
    while (received < length) {
        const auto bytes = ::recv(fd, data + received, length - received, 0);
        if (bytes <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(bytes);
    }
    return true;
}

void WebSocketServer::broadcast(const std::string& jsonMessage) {
    std::vector<SessionPtr> sessionsCopy;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessionsCopy = sessions_;
    }

    std::vector<SessionPtr> toRemove;
    for (const auto& session : sessionsCopy) {
        if (!session || !session->active.load()) {
            toRemove.push_back(session);
            continue;
        }
        if (!sendTextFrame(session, jsonMessage)) {
            closeWithReason(session, kCloseCodeAbnormal, "write_error", "write_error");
            toRemove.push_back(session);
        }
    }

    if (!toRemove.empty()) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.erase(std::remove_if(sessions_.begin(),
                                       sessions_.end(),
                                       [&toRemove](const SessionPtr& session) {
                                           return std::any_of(toRemove.begin(),
                                                              toRemove.end(),
                                                              [&session](const SessionPtr& other) {
                                                                  return other.get() == session.get();
                                                              });
                                       }),
                        sessions_.end());
    }
}

void broadcast(const std::string& jsonMessage) {
    WebSocketServer::instance().broadcast(jsonMessage);
}

std::vector<WebSocketServer::SessionSnapshot> WebSocketServer::getSessionSnapshots() {
    std::vector<SessionPtr> sessionsCopy;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessionsCopy = sessions_;
    }

    std::vector<SessionSnapshot> snapshots;
    snapshots.reserve(sessionsCopy.size());
    for (const auto& session : sessionsCopy) {
        if (!session) {
            continue;
        }
        snapshots.push_back(session->snapshot());
    }

    return snapshots;
}

void WebSocketServer::keepAliveLoop() {
    const auto inactivityTimeoutMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(kInactivityTimeout).count();

    std::unique_lock<std::mutex> lock(keepAliveMutex_);
    while (running_.load()) {
        const auto pingIntervalCount = std::max<std::int64_t>(1, pingPeriodMs_.load(std::memory_order_relaxed));
        const auto waitDuration = std::chrono::milliseconds(pingIntervalCount);
        keepAliveCv_.wait_for(lock, waitDuration, [this]() { return !running_.load(); });
        if (!running_.load()) {
            break;
        }

        lock.unlock();

        const auto nowMs = steadyNowMs();
        const auto nowSteady = std::chrono::steady_clock::now();
        const auto pongTimeoutCount = std::max<std::int64_t>(1, pongTimeoutMs_.load(std::memory_order_relaxed));
        const auto pongTimeout = std::chrono::milliseconds(pongTimeoutCount);

        std::vector<SessionPtr> sessionsCopy;
        {
            std::lock_guard<std::mutex> sessionsLock(sessionsMutex_);
            sessionsCopy = sessions_;
        }

        std::vector<SessionPtr> toRemove;
        for (const auto& session : sessionsCopy) {
            if (!session || !session->active.load()) {
                continue;
            }

            const auto sessionFd = session->fd.load(std::memory_order_relaxed);
            const auto lastActivity = session->lastActivityMs.load(std::memory_order_relaxed);
            if (nowMs - lastActivity >= inactivityTimeoutMs) {
                LOG_INFO("WS session(" << sessionFd << ") cerrada por inactividad");
                closeWithReason(session, kCloseCodeGoingAway, "inactivity", "inactivity");
                toRemove.push_back(session);
                continue;
            }

            int consecutiveMisses = 0;
            std::chrono::milliseconds sinceLastPong{0};
            const bool pongExceeded =
                session->updatePongTimeout(nowSteady, pongTimeout, consecutiveMisses, sinceLastPong);
            if (pongExceeded) {
                if (consecutiveMisses >= 2) {
                    LOG_DEBUG("WS session(" << sessionFd
                                            << ") close_code=going_away dead_reason=pong_timeout "
                                            << "consecutive_pong_misses=" << consecutiveMisses
                                            << " last_pong_ago_ms=" << sinceLastPong.count());
                    closeWithReason(session, kCloseCodeGoingAway, "pong_timeout", "pong_timeout");
                    toRemove.push_back(session);
                    continue;
                }

                LOG_WARN("WS session(" << sessionFd << ") sin PONG por " << sinceLastPong.count()
                                        << "ms (pong_timeout=" << pongTimeoutCount << "ms)");
            }

            const auto lastPing = session->lastPingMs.load(std::memory_order_relaxed);
            if (nowMs - lastPing >= pingIntervalCount) {
                if (sendPingFrame(session)) {
                    session->lastPingMs.store(nowMs, std::memory_order_relaxed);
                    session->recordPingSent();
                } else {
                    closeWithReason(session, kCloseCodeAbnormal, "write_error", "write_error");
                    toRemove.push_back(session);
                }
            }
        }

        for (const auto& session : toRemove) {
            removeSession(session);
        }

        lock.lock();
    }
}

void WebSocketServer::recordMessageReceived_() {
    ttp::common::metrics::Registry::instance().incrementCounter("ws.messages_received");

    std::lock_guard<std::mutex> lock(statsMutex_);
    ++stats_.messagesReceived;
}

void WebSocketServer::recordMessageSent_() {
    ttp::common::metrics::Registry::instance().incrementCounter("ws.messages_sent");

    std::lock_guard<std::mutex> lock(statsMutex_);
    ++stats_.messagesSent;
}

void WebSocketServer::recordCloseReason_(const std::string& deadReasonTag) {
    const auto now = std::chrono::steady_clock::now();
    std::string counterKey;

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        if (deadReasonTag == "pong_timeout") {
            ++stats_.closePongTimeout;
            counterKey = "ws.close.pong_timeout";
        }
        else if (deadReasonTag == "backpressure") {
            ++stats_.closeBackpressure;
            counterKey = "ws.close.backpressure";
        }
        else if (deadReasonTag == "read_error") {
            ++stats_.closeReadError;
            counterKey = "ws.close.read_error";
        }
        else if (deadReasonTag == "write_error") {
            ++stats_.closeWriteError;
            counterKey = "ws.close.write_error";
        }

        const std::uint64_t totalCloses = stats_.closePongTimeout + stats_.closeBackpressure
            + stats_.closeReadError + stats_.closeWriteError;

        if (totalCloses > 0 && now - stats_.lastSummaryLog >= kCloseSummaryInterval) {
            stats_.lastSummaryLog = now;
            LOG_INFO("ws_close_summary total="
                     << totalCloses << " pong_timeout=" << stats_.closePongTimeout
                     << " backpressure=" << stats_.closeBackpressure
                     << " read_error=" << stats_.closeReadError
                     << " write_error=" << stats_.closeWriteError
                     << " messages_sent=" << stats_.messagesSent
                     << " messages_received=" << stats_.messagesReceived);
        }
    }

    if (!counterKey.empty()) {
        ttp::common::metrics::Registry::instance().incrementCounter(counterKey);
    }
}

}  // namespace ttp::api
