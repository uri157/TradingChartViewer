#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#if __has_include(<boost/asio/executor_work_guard.hpp>)
#include <boost/asio/executor_work_guard.hpp>
#endif
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json/value.hpp>

#include "domain/exchange/IExchangeKlines.hpp"

namespace adapters::binance {

class BinanceWsClient : public domain::IExchangeLiveKlines {
public:
    BinanceWsClient();
    ~BinanceWsClient() override;

    void subscribe(const std::vector<std::string>& symbols,
                   domain::Interval interval,
                   std::function<void(const std::string&, const domain::Candle&)> on_closed_candle) override;

    void set_on_reconnected(std::function<void()> callback) override;

    void stop() override;

private:
    using WsStream = boost::beast::websocket::stream<
        boost::asio::ssl::stream<boost::beast::tcp_stream>>;

    class WorkGuard {
    public:
        explicit WorkGuard(boost::asio::io_context& ctx);
        void reset();

    private:
#if __has_include(<boost/asio/executor_work_guard.hpp>)
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard_;
#else
        std::unique_ptr<boost::asio::io_context::work> work_;
#endif
    };

    void run_(std::vector<std::string> symbols, domain::Interval interval);
    void process_message_(const std::string& payload);
    static std::string normalize_symbol_(const std::string& symbol);
    static double parse_json_number_(const boost::json::value& value);
    static std::int64_t parse_json_int_(const boost::json::value& value);

    std::atomic<bool> running_{true};
    std::atomic<bool> subscribed_{false};
    std::atomic<std::chrono::steady_clock::time_point> last_msg_tp_{
        std::chrono::steady_clock::now()};
    std::function<void(const std::string&, const domain::Candle&)> on_closed_candle_;
    std::function<void()> on_reconnected_;
    std::mutex callback_mutex_;
    std::thread worker_;

    std::mutex ws_mutex_;
    std::shared_ptr<WsStream> active_ws_;
    std::shared_ptr<boost::asio::steady_timer> ping_timer_;
    std::shared_ptr<boost::asio::steady_timer> silence_timer_;
    std::shared_ptr<boost::asio::io_context> active_ioc_;
    std::shared_ptr<WorkGuard> work_guard_;
    std::shared_ptr<std::thread> io_thread_;
};

}  // namespace adapters::binance

