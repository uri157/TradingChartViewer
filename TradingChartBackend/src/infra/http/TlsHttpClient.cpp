#include "infra/http/TlsHttpClient.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <openssl/err.h>

namespace infra::http {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

constexpr int kMaxRedirects = 5;

std::runtime_error makeError(const std::string& host, const std::string& target, const std::string& message) {
    std::ostringstream oss;
    oss << "HTTPS GET request to https://" << host << target << " failed: " << message;
    return std::runtime_error(oss.str());
}

struct ParsedLocation {
    std::string host;
    std::string target;
};

ParsedLocation parseRedirectLocation(const std::string& location, const std::string& currentHost) {
    if (location.empty()) {
        throw std::runtime_error("Redirect response missing Location header");
    }

    ParsedLocation result{};

    if (location.rfind("https://", 0) == 0) {
        const std::string withoutScheme = location.substr(std::string{"https://"}.size());
        const auto slashPos = withoutScheme.find('/');
        std::string hostPart = slashPos == std::string::npos ? withoutScheme : withoutScheme.substr(0, slashPos);
        if (hostPart.empty()) {
            throw std::runtime_error("Redirect URL missing host");
        }
        const auto colonPos = hostPart.find(':');
        if (colonPos != std::string::npos) {
            const std::string portPart = hostPart.substr(colonPos + 1);
            if (portPart != "443") {
                throw std::runtime_error("Redirect to unsupported HTTPS port: " + portPart);
            }
            hostPart = hostPart.substr(0, colonPos);
        }
        result.host = hostPart;
        if (slashPos == std::string::npos) {
            result.target = "/";
        } else {
            result.target = withoutScheme.substr(slashPos);
        }
    } else if (location.rfind("http://", 0) == 0) {
        throw std::runtime_error("Insecure redirect to HTTP is not supported");
    } else {
        result.host = currentHost;
        if (!location.empty() && location.front() == '/') {
            result.target = location;
        } else {
            result.target = "/" + location;
        }
    }

    if (result.target.empty()) {
        result.target = "/";
    }

    return result;
}

http::response<http::string_body> performRequest(const std::string& host, const std::string& target, int timeoutSec) {
    if (timeoutSec <= 0) {
        throw makeError(host, target, "timeout must be positive");
    }

    net::io_context ioc;
    ssl::context sslContext(ssl::context::tls_client);
    // TODO: Enable certificate verification once a CA bundle is bundled with the project.
    sslContext.set_verify_mode(ssl::verify_none);

    ssl::stream<beast::tcp_stream> stream(ioc, sslContext);

    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
        const unsigned long err = ::ERR_get_error();
        const char* reason = err != 0 ? ::ERR_reason_error_string(err) : nullptr;
        std::ostringstream oss;
        oss << "Failed to set SNI hostname to '" << host << "'";
        if (reason != nullptr) {
            oss << ": " << reason;
        }
        throw makeError(host, target, oss.str());
    }

    auto resolver = net::ip::tcp::resolver(ioc);
    beast::error_code ec;
    auto const results = resolver.resolve(host, "443", ec);
    if (ec) {
        throw makeError(host, target, "DNS resolution error: " + ec.message());
    }

    auto& lowestLayer = beast::get_lowest_layer(stream);
    lowestLayer.expires_after(std::chrono::seconds(timeoutSec));
    lowestLayer.connect(results, ec);
    if (ec) {
        throw makeError(host, target, "Connection error: " + ec.message());
    }

    lowestLayer.expires_after(std::chrono::seconds(timeoutSec));
    stream.handshake(ssl::stream_base::client, ec);
    if (ec) {
        throw makeError(host, target, "TLS handshake error: " + ec.message());
    }

    http::request<http::empty_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "TTP/0.1 (+https://local)");
    req.set(http::field::accept, "application/json");
    req.set(http::field::connection, "close");

    http::write(stream, req, ec);
    if (ec) {
        throw makeError(host, target, "Write error: " + ec.message());
    }

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    lowestLayer.expires_after(std::chrono::seconds(timeoutSec));
    http::read(stream, buffer, response, ec);
    if (ec) {
        throw makeError(host, target, "Read error: " + ec.message());
    }

    stream.shutdown(ec);
    if (ec == net::error::eof) {
        ec = {};
    }
    if (ec == ssl::error::stream_truncated) {
        // Allow truncated TLS shutdown which may occur with some servers.
        ec = {};
    }
    if (ec) {
        throw makeError(host, target, "TLS shutdown error: " + ec.message());
    }

    return response;
}

}  // namespace

JsonResponse https_get_json_response(const std::string& host, const std::string& target, int timeout_sec) {
    if (host.empty()) {
        throw std::runtime_error("HTTPS GET requires a non-empty host");
    }

    std::string normalizedTarget = target.empty() ? std::string{"/"} : target;
    if (!normalizedTarget.empty() && normalizedTarget.front() != '/') {
        normalizedTarget.insert(normalizedTarget.begin(), '/');
    }

    std::string currentHost = host;
    std::string currentTarget = normalizedTarget;

    for (int redirectCount = 0; redirectCount <= kMaxRedirects; ++redirectCount) {
        auto response = performRequest(currentHost, currentTarget, timeout_sec);
        const auto status = static_cast<unsigned>(response.result_int());
        if (status == 301U || status == 302U) {
            try {
                const auto locationHeader = response.base()[http::field::location];
                const auto parsed = parseRedirectLocation(std::string(locationHeader), currentHost);
                currentHost = parsed.host;
                currentTarget = parsed.target;
                continue;
            } catch (const std::exception& redirectError) {
                throw makeError(currentHost, currentTarget, redirectError.what());
            }
        }

        JsonResponse result{};
        result.status = status;
        result.body = std::move(response.body());
        result.final_host = currentHost;
        result.final_target = currentTarget;
        if (auto it = response.base().find("X-MBX-USED-WEIGHT"); it != response.base().end()) {
            result.used_weight_header = std::string{it->value()};
        }
        return result;
    }

    throw makeError(currentHost, currentTarget, "Too many redirects");
}

std::string https_get_json(const std::string& host, const std::string& target, int timeout_sec) {
    const auto response = https_get_json_response(host, target, timeout_sec);
    if (response.status >= 400U) {
        const std::string& errorHost = response.final_host.empty() ? host : response.final_host;
        const std::string& errorTarget = response.final_target.empty()
                                        ? (target.empty() ? std::string{"/"} : target)
                                        : response.final_target;
        throw makeError(errorHost, errorTarget,
                        "HTTP status " + std::to_string(response.status) + " received");
    }
    return response.body;
}

}  // namespace infra::http
