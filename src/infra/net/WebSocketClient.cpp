#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#define _CRT_SECURE_NO_WARNINGS
#include "infra/net/WebSocketClient.h"

#include <boost/asio/strand.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <openssl/err.h>
#include "logging/Log.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace json = boost::json;
using tcp = asio::ip::tcp;

namespace {
std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string formatWsTarget(const std::string& templ, const std::string& lowerSymbol, const std::string& interval) {
    if (templ.find("%s") == std::string::npos) {
        std::string base = templ.empty() ? std::string{"/ws"} : templ;
        if (!base.empty() && base.back() != '/') {
            base.push_back('/');
        }
        return base + lowerSymbol + "@kline_" + interval;
    }

    std::string result;
    result.reserve(templ.size() + lowerSymbol.size() + interval.size());
    int placeholderIndex = 0;
    for (std::size_t i = 0; i < templ.size();) {
        if (templ[i] == '%' && i + 1 < templ.size() && templ[i + 1] == 's') {
            if (placeholderIndex == 0) {
                result += lowerSymbol;
            }
            else if (placeholderIndex == 1) {
                result += interval;
            }
            else {
                result += "%s";
            }
            ++placeholderIndex;
            i += 2;
        }
        else {
            result.push_back(templ[i++]);
        }
    }

    if (!result.empty() && result.front() != '/') {
        result.insert(result.begin(), '/');
    }
    return result;
}
}  // namespace

namespace infra::net {

WebSocketClient::WebSocketClient(const std::string& symbolParam,
                                 const std::string& intervalParam,
                                 const std::string& hostOverride,
                                 const std::string& pathTemplate)
    : symbol(symbolParam), interval(intervalParam), resolver(asio::make_strand(ioc)), ctx(ssl::context::tlsv12_client),
      reconnectTimer(ioc) {
    host = hostOverride.empty() ? host : hostOverride;
    std::string lowercaseSymbol = toLowerCopy(symbol);
    std::string templ = pathTemplate.empty() ? std::string{"/ws/%s@kline_%s"} : pathTemplate;
    target = formatWsTarget(templ, lowercaseSymbol, interval);
    if (target.empty()) {
        target = "/ws/" + lowercaseSymbol + "@kline_" + interval;
    }

    ctx.set_verify_mode(ssl::verify_none);
    createWebSocket();
}

WebSocketClient::~WebSocketClient() {
    disconnect();
}

void WebSocketClient::setDataHandler(const std::function<void(const infra::storage::PriceData&)>& handler) {
    dataHandler = handler;
}

bool WebSocketClient::isWsConnected() const noexcept {
    return connected.load(std::memory_order_acquire) && wsOpen_.load(std::memory_order_acquire);
}

domain::TimestampMs WebSocketClient::lastTickMs() const noexcept {
    return lastTickMs_.load(std::memory_order_acquire);
}

void WebSocketClient::createWebSocket() {
    ws = std::make_unique<WebSocketStream>(asio::make_strand(ioc), ctx);
    reconnectTimer.cancel();
}

void WebSocketClient::resetBackoff() {
    reconnectDelay = std::chrono::seconds(1);
}

void WebSocketClient::connect() {
    std::lock_guard<std::mutex> lock(connectionMutex);
    if (connected) {
        return;
    }
    connected = true;
    wsOpen_.store(false, std::memory_order_release);
    lastTickMs_.store(0, std::memory_order_release);
    resetBackoff();
    buffer.consume(buffer.size());
    ioc.restart();
    createWebSocket();

    ioThread = std::thread([this]() { run(); });
}

void WebSocketClient::disconnect() {
    std::lock_guard<std::mutex> lock(connectionMutex);
    if (!connected) {
        return;
    }
    connected = false;
    wsOpen_.store(false, std::memory_order_release);

    reconnectTimer.cancel();
    asio::post(ioc, [self = this]() {
        if (self->ws) {
            beast::error_code ec;
            self->ws->close(websocket::close_code::normal, ec);
        }
    });

    ioc.stop();
    if (ioThread.joinable()) {
        ioThread.join();
    }
}

void WebSocketClient::run() {
    resolver.async_resolve(host, port,
        beast::bind_front_handler(&WebSocketClient::onResolve, this));
    ioc.run();
}

void WebSocketClient::handleFailure(const std::string& stage, beast::error_code ec) {
    if (!connected.load()) {
        return;
    }
    wsOpen_.store(false, std::memory_order_release);
    LOG_WARN(logging::LogCategory::NET, "WebSocket error during %s: %s", stage.c_str(), ec.message().c_str());
    scheduleReconnect();
}

void WebSocketClient::scheduleReconnect() {
    if (!connected.load()) {
        return;
    }

    wsOpen_.store(false, std::memory_order_release);

    reconnectDelay = std::min(reconnectDelay * 2, maxReconnectDelay);
    reconnectTimer.expires_after(reconnectDelay);
    reconnectTimer.async_wait(beast::bind_front_handler(&WebSocketClient::onReconnectTimer, this));
}

void WebSocketClient::onReconnectTimer(beast::error_code ec) {
    if (ec || !connected.load()) {
        return;
    }

    buffer.consume(buffer.size());
    createWebSocket();
    resolver.async_resolve(host, port,
        beast::bind_front_handler(&WebSocketClient::onResolve, this));
}

void WebSocketClient::onResolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec) {
        handleFailure("resolve", ec);
        return;
    }

    if (!ws) {
        createWebSocket();
    }

    beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(30));

    beast::get_lowest_layer(*ws).async_connect(
        results,
        beast::bind_front_handler(&WebSocketClient::onConnect, this));
}

void WebSocketClient::onConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
    if (ec) {
        handleFailure("connect", ec);
        return;
    }

    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str())) {
        beast::error_code sniEc{ static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category() };
        handleFailure("sni", sniEc);
        return;
    }

    ws->next_layer().async_handshake(
        ssl::stream_base::client,
        beast::bind_front_handler(&WebSocketClient::onSslHandshake, this));
}

void WebSocketClient::onSslHandshake(beast::error_code ec) {
    if (ec) {
        handleFailure("ssl_handshake", ec);
        return;
    }

    beast::get_lowest_layer(*ws).expires_never();

    ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-cpp");
    }));

    ws->async_handshake(host, target,
        beast::bind_front_handler(&WebSocketClient::onHandshake, this));
}

void WebSocketClient::onHandshake(beast::error_code ec) {
    if (ec) {
        handleFailure("handshake", ec);
        return;
    }

    resetBackoff();
    wsOpen_.store(true, std::memory_order_release);
    doRead();
}

void WebSocketClient::doRead() {
    ws->async_read(buffer,
        beast::bind_front_handler(&WebSocketClient::onRead, this));
}

void WebSocketClient::onRead(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        if (ec == websocket::error::closed) {
            wsOpen_.store(false, std::memory_order_release);
            scheduleReconnect();
            return;
        }
        handleFailure("read", ec);
        return;
    }

    std::string message = beast::buffers_to_string(buffer.data());
    buffer.consume(buffer.size());

    try {
        json::value jsonData = json::parse(message);
        if (jsonData.is_object()) {
            const json::object& obj = jsonData.as_object();
            if (obj.contains("e") && obj.at("e").as_string() == "kline") {
                const json::object& kline = obj.at("k").as_object();

                infra::storage::PriceData data;
                data.openTime = kline.at("t").as_int64();
                data.closeTime = kline.at("T").as_int64();
                std::strncpy(data.symbol, symbol.c_str(), sizeof(data.symbol) - 1);
                data.symbol[sizeof(data.symbol) - 1] = '\0';
                std::strncpy(data.interval, interval.c_str(), sizeof(data.interval) - 1);
                data.interval[sizeof(data.interval) - 1] = '\0';

                data.openPrice = std::stod(std::string(kline.at("o").as_string().data(), kline.at("o").as_string().size()));
                data.closePrice = std::stod(std::string(kline.at("c").as_string().data(), kline.at("c").as_string().size()));
                data.highPrice = std::stod(std::string(kline.at("h").as_string().data(), kline.at("h").as_string().size()));
                data.lowPrice = std::stod(std::string(kline.at("l").as_string().data(), kline.at("l").as_string().size()));
                data.volume = std::stod(std::string(kline.at("v").as_string().data(), kline.at("v").as_string().size()));
                data.baseAssetVolume = std::stod(std::string(kline.at("q").as_string().data(), kline.at("q").as_string().size()));
                data.numberOfTrades = static_cast<int>(kline.at("n").as_int64());
                data.takerBuyVolume = std::stod(std::string(kline.at("V").as_string().data(), kline.at("V").as_string().size()));
                data.takerBuyBaseAssetVolume = std::stod(std::string(kline.at("Q").as_string().data(), kline.at("Q").as_string().size()));

                lastTickMs_.store(data.closeTime, std::memory_order_release);
                if (dataHandler) {
                    dataHandler(data);
                }
            }
        }
    }
    catch (const std::exception& ex) {
        LOG_WARN(logging::LogCategory::NET, "Error parsing WebSocket message: %s", ex.what());
    }

    doRead();
}

}  // namespace infra::net
