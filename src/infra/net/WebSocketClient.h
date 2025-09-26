// WebSocketClient.h
#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include <memory>
#include "infra/storage/PriceData.h"
#include "domain/Types.h"

namespace infra::net {

class WebSocketClient {
public:
    WebSocketClient(const std::string& symbol,
                    const std::string& interval,
                    const std::string& host,
                    const std::string& pathTemplate);
    ~WebSocketClient();

    void connect();
    void disconnect();

    void setDataHandler(const std::function<void(const infra::storage::PriceData&)>& handler);

    bool isWsConnected() const noexcept;
    domain::TimestampMs lastTickMs() const noexcept;

private:
    void run();
    void onResolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void onConnect(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type::endpoint_type);
    void onSslHandshake(boost::beast::error_code ec);
    void onHandshake(boost::beast::error_code ec);
    void doRead();
    void onRead(boost::beast::error_code ec, std::size_t bytes_transferred);
    void scheduleReconnect();
    void onReconnectTimer(boost::beast::error_code ec);
    void createWebSocket();
    void resetBackoff();
    void handleFailure(const std::string& stage, boost::beast::error_code ec);

    std::string symbol;
    std::string interval;
    std::string host = "stream.binance.com";
    std::string port = "9443";
    std::string target;

    std::function<void(const infra::storage::PriceData&)> dataHandler;

    boost::asio::io_context ioc;
    boost::asio::ip::tcp::resolver resolver;
    boost::asio::ssl::context ctx;
    using WebSocketStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;
    std::unique_ptr<WebSocketStream> ws;
    boost::beast::flat_buffer buffer;
    boost::asio::steady_timer reconnectTimer;
    std::chrono::seconds reconnectDelay{1};
    const std::chrono::seconds maxReconnectDelay{30};

    std::thread ioThread;
    std::atomic<bool> connected{ false };
    std::atomic<bool> wsOpen_{ false };
    std::atomic<domain::TimestampMs> lastTickMs_{0};
    std::mutex connectionMutex;
};

}  // namespace infra::net
