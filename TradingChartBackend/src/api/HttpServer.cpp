#include "api/HttpServer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "common/Log.hpp"
#include "api/WebSocketServer.hpp"

namespace ttp::api {

namespace {

std::string describeErrno(int err) {
    return std::strerror(err);
}

std::string formatAddress(const Endpoint& endpoint) {
    if (endpoint.address.empty()) {
        return std::string("0.0.0.0:") + std::to_string(endpoint.port);
    }
    return endpoint.address + ':' + std::to_string(endpoint.port);
}

}  // namespace

HttpServer::HttpServer(IoContext& ioContext, Endpoint endpoint, std::size_t threadCount)
    : ioContext_(ioContext), endpoint_(std::move(endpoint)), threadCount_(threadCount ? threadCount : 1) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::setCorsConfig(CorsConfig config) { corsConfig_ = std::move(config); }

void HttpServer::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    serverFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        running_.store(false);
        throw std::runtime_error("No se pudo crear el socket del servidor: " + describeErrno(errno));
    }

    int opt = 1;
    ::setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint_.port);
    if (endpoint_.address.empty() || endpoint_.address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, endpoint_.address.c_str(), &addr.sin_addr) != 1) {
            ::close(serverFd_);
            serverFd_ = -1;
            running_.store(false);
            throw std::runtime_error("Dirección inválida: " + endpoint_.address);
        }
    }

    if (::bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const auto message = describeErrno(errno);
        ::close(serverFd_);
        serverFd_ = -1;
        running_.store(false);
        throw std::runtime_error("No se pudo enlazar el socket: " + message);
    }

    if (::listen(serverFd_, SOMAXCONN) < 0) {
        const auto message = describeErrno(errno);
        ::close(serverFd_);
        serverFd_ = -1;
        running_.store(false);
        throw std::runtime_error("No se pudo iniciar la escucha: " + message);
    }

    workGuard_.emplace(ioContext_.makeWorkGuard());

    LOG_INFO("HTTP server escuchando en " << formatAddress(endpoint_));

    threads_.reserve(threadCount_);
    for (std::size_t i = 0; i < threadCount_; ++i) {
        threads_.emplace_back([this, i]() { workerLoop(i); });
    }
}

void HttpServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    ioContext_.stop();

    if (serverFd_ >= 0) {
        ::shutdown(serverFd_, SHUT_RDWR);
        ::close(serverFd_);
        serverFd_ = -1;
    }

    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();

    workGuard_.reset();
}

void HttpServer::wait() {
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

void HttpServer::workerLoop(std::size_t workerId) {
    LOG_DEBUG("Worker " << workerId << " iniciado");

    while (running_.load()) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = ::accept(serverFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (!running_.load()) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EBADF || errno == EINVAL) {
                break;
            }
            LOG_WARN("Error aceptando conexión: " << describeErrno(errno));
            continue;
        }

        handleClient(clientFd);
    }

    LOG_DEBUG("Worker " << workerId << " finalizado");
}

void HttpServer::handleClient(int clientFd) {
    std::string request;
    request.reserve(1024);
    char buffer[1024];

    while (request.find("\r\n\r\n") == std::string::npos) {
        const auto bytes = ::recv(clientFd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            break;
        }
        request.append(buffer, static_cast<std::size_t>(bytes));
        if (request.size() > 8192) {
            break;
        }
    }

    std::istringstream requestStream(request);
    std::string requestLine;
    std::getline(requestStream, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') {
        requestLine.pop_back();
    }

    std::istringstream lineStream(requestLine);
    std::string method;
    std::string target;
    std::string version;
    lineStream >> method >> target >> version;

    Request apiRequest{};
    apiRequest.method = method;
    apiRequest.target = target;
    apiRequest.version = version;

    const auto queryPos = target.find('?');
    if (queryPos != std::string::npos) {
        apiRequest.path = target.substr(0, queryPos);
        apiRequest.query = target.substr(queryPos + 1);
    } else {
        apiRequest.path = target;
    }

    if (WebSocketServer::instance().handleClient(clientFd, request, apiRequest)) {
        return;
    }

    const auto responseData = router_.handle(apiRequest);
    const auto& statusCode = responseData.statusCode;
    const auto& statusText = responseData.statusText;
    const auto& body = responseData.body;

    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << ' ' << statusText << "\r\n";
    const std::string contentType = responseData.contentType.empty() ? "application/json" : responseData.contentType;
    response << "Content-Type: " << contentType << "\r\n";
    if (!responseData.headers.empty()) {
        for (const auto& header : responseData.headers) {
            if (!header.first.empty()) {
                response << header.first << ": " << header.second << "\r\n";
            }
        }
    }
    if (corsConfig_.enabled && !corsConfig_.origin.empty()) {
        response << "Access-Control-Allow-Origin: " << corsConfig_.origin << "\r\n";
        response << "Vary: Origin\r\n";
        response << "Access-Control-Allow-Headers: Content-Type\r\n";
    }
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;

    const auto responseStr = response.str();
    const char* data = responseStr.data();
    std::size_t remaining = responseStr.size();

    while (remaining > 0) {
        const auto written = ::send(clientFd, data, remaining, 0);
        if (written <= 0) {
            break;
        }
        remaining -= static_cast<std::size_t>(written);
        data += written;
    }

    ::shutdown(clientFd, SHUT_RDWR);
    ::close(clientFd);
}

}  // namespace ttp::api
