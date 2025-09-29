#pragma once

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace httplib {

struct Request {
    std::string                                   method;
    std::string                                   path;
    std::string                                   version;
    std::map<std::string, std::string>            params;
    std::string                                   body;

    [[nodiscard]] bool has_param(const std::string& key) const {
        return params.find(key) != params.end();
    }

    [[nodiscard]] std::string get_param_value(const std::string& key) const {
        auto it = params.find(key);
        if (it == params.end()) {
            throw std::out_of_range("parameter not found");
        }
        return it->second;
    }
};

struct Response {
    int                              status{200};
    std::string                      body;
    std::map<std::string, std::string> headers;

    void set_content(const std::string& content, const std::string& content_type) {
        body = content;
        headers["Content-Type"] = content_type;
    }

    void set_header(const std::string& key, const std::string& value) {
        headers[key] = value;
    }
};

using Handler = std::function<void(const Request&, Response&)>;

inline std::string url_decode(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == '%') {
            if (i + 2 >= text.size()) {
                break;
            }
            const auto hex = text.substr(i + 1, 2);
            char*       end = nullptr;
            long        value = std::strtol(std::string(hex).c_str(), &end, 16);
            if (end != nullptr && *end == '\0') {
                result.push_back(static_cast<char>(value));
                i += 2;
            }
            else {
                result.push_back('%');
            }
        }
        else if (ch == '+') {
            result.push_back(' ');
        }
        else {
            result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

inline std::map<std::string, std::string> parse_query(std::string_view query) {
    std::map<std::string, std::string> params;
    std::size_t                        start = 0;
    while (start < query.size()) {
        auto end = query.find('&', start);
        if (end == std::string::npos) {
            end = query.size();
        }
        const auto pair = query.substr(start, end - start);
        auto       eq   = pair.find('=');
        if (eq == std::string::npos) {
            params.emplace(url_decode(pair), std::string());
        }
        else {
            params.emplace(url_decode(pair.substr(0, eq)), url_decode(pair.substr(eq + 1)));
        }
        start = end + 1;
    }
    return params;
}

inline const char* reason_phrase(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 500:
        return "Internal Server Error";
    default:
        return "OK";
    }
}

class Server {
public:
    Server()
        : server_fd_{-1}, running_{false}, is_bound_{false}, bound_port_{0} {}

    ~Server() { stop(); }

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void Get(const std::string& pattern, Handler handler) {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        get_handlers_[pattern] = std::move(handler);
    }

    bool listen(const std::string& host, int port) {
        if (!bind_to_port(host, port)) {
            return false;
        }
        return listen_after_bind();
    }

    bool bind_to_port(const std::string& host, int port) {
        if (is_bound_) {
            return false;
        }

        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }

        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(static_cast<unsigned short>(port));
        if (host.empty() || host == "0.0.0.0") {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        else {
            if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
                ::close(fd);
                return false;
            }
        }

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            return false;
        }

        if (::listen(fd, 16) < 0) {
            ::close(fd);
            return false;
        }

        server_fd_  = fd;
        is_bound_   = true;
        bound_port_ = port;
        return true;
    }

    bool listen_after_bind() {
        if (!is_bound_ || server_fd_ < 0) {
            return false;
        }

        running_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_acquire)) {
            sockaddr_in client_addr{};
            socklen_t   client_len = sizeof(client_addr);
            const int   client_fd  = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd < 0) {
                if (!running_.load(std::memory_order_acquire)) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EBADF) {
                    break;
                }
                continue;
            }

            std::lock_guard<std::mutex> lock(worker_mutex_);
            worker_threads_.emplace_back([this, client_fd]() { handle_client(client_fd); });
        }

        cleanup_workers();

        if (server_fd_ >= 0) {
            ::close(server_fd_);
            server_fd_ = -1;
        }
        is_bound_ = false;
        return true;
    }

    void stop() {
        const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
        if (server_fd_ >= 0) {
            ::shutdown(server_fd_, SHUT_RDWR);
            ::close(server_fd_);
            server_fd_ = -1;
        }
        if (wasRunning) {
            // connect to unblock accept if necessary
            if (bound_port_ != 0) {
                const int dummy = ::socket(AF_INET, SOCK_STREAM, 0);
                if (dummy >= 0) {
                    sockaddr_in addr{};
                    addr.sin_family      = AF_INET;
                    addr.sin_port        = htons(static_cast<unsigned short>(bound_port_));
                    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    ::connect(dummy, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
                    ::close(dummy);
                }
            }
        }
        cleanup_workers();
        is_bound_   = false;
        bound_port_ = 0;
    }

private:
    void cleanup_workers() {
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            workers.swap(worker_threads_);
        }
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void handle_client(int client_fd) {
        std::string request_data;
        request_data.reserve(1024);
        char buffer[2048];
        while (true) {
            const ssize_t received = ::recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                break;
            }
            request_data.append(buffer, static_cast<std::size_t>(received));
            if (request_data.find("\r\n\r\n") != std::string::npos) {
                break;
            }
            if (request_data.size() > 32 * 1024) {
                break;
            }
        }

        Request req;
        Response res;

        parse_request(request_data, req);
        dispatch_request(req, res);
        send_response(client_fd, res);
        ::shutdown(client_fd, SHUT_RDWR);
        ::close(client_fd);
    }

    void parse_request(const std::string& raw, Request& req) {
        std::istringstream stream(raw);
        std::string        request_line;
        if (!std::getline(stream, request_line)) {
            return;
        }
        if (!request_line.empty() && request_line.back() == '\r') {
            request_line.pop_back();
        }

        std::istringstream request_line_stream(request_line);
        std::string        target;
        request_line_stream >> req.method >> target >> req.version;

        auto query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            req.path   = target.substr(0, query_pos);
            req.params = parse_query(target.substr(query_pos + 1));
        }
        else {
            req.path = target;
        }

        std::string header_line;
        while (std::getline(stream, header_line)) {
            if (header_line == "\r" || header_line.empty()) {
                break;
            }
        }

        std::string body;
        if (std::getline(stream, body, '\0')) {
            req.body = body;
        }
    }

    void dispatch_request(const Request& req, Response& res) {
        Handler handler;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            auto                         it = get_handlers_.find(req.path);
            if (it != get_handlers_.end()) {
                handler = it->second;
            }
        }

        if (!handler) {
            res.status = 404;
            res.body   = "Not found";
            res.headers["Content-Type"] = "text/plain";
            return;
        }

        try {
            handler(req, res);
        }
        catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(std::string("Internal error: ") + ex.what(), "text/plain");
        }
        catch (...) {
            res.status = 500;
            res.set_content("Internal error", "text/plain");
        }
    }

    void send_response(int client_fd, const Response& res) {
        std::ostringstream response_stream;
        response_stream << "HTTP/1.1 " << res.status << ' ' << reason_phrase(res.status) << "\r\n";

        bool has_content_length = false;
        for (const auto& [key, value] : res.headers) {
            if (key == "Content-Length") {
                has_content_length = true;
            }
            response_stream << key << ": " << value << "\r\n";
        }

        if (!has_content_length) {
            response_stream << "Content-Length: " << res.body.size() << "\r\n";
        }
        response_stream << "Connection: close\r\n\r\n";
        response_stream << res.body;

        const std::string response_text = response_stream.str();
        const char*       data          = response_text.data();
        std::size_t       total_sent    = 0;
        while (total_sent < response_text.size()) {
            const ssize_t sent = ::send(client_fd, data + total_sent, response_text.size() - total_sent, 0);
            if (sent <= 0) {
                break;
            }
            total_sent += static_cast<std::size_t>(sent);
        }
    }

    int                       server_fd_;
    std::atomic<bool>         running_;
    bool                      is_bound_;
    int                       bound_port_;
    std::map<std::string, Handler> get_handlers_;
    std::mutex                handler_mutex_;
    std::vector<std::thread>  worker_threads_;
    std::mutex                worker_mutex_;
};

}  // namespace httplib

