#include "adapters/binance/BinanceWsClient.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <future>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/rfc2818_verification.hpp>
#if __has_include(<boost/asio/executor_work_guard.hpp>)
#include <boost/asio/executor_work_guard.hpp>
#endif
#include <boost/beast/core.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/error.hpp>
#include <boost/json.hpp>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "logging/Log.h"
#include "common/Metrics.hpp"

namespace adapters::binance {
namespace {
constexpr const char* kHost = "stream.binance.com";
constexpr const char* kPort = "9443";
constexpr const char* kBasePath = "/stream?streams=";
constexpr std::chrono::milliseconds kBackoffBase{1000};
constexpr std::chrono::milliseconds kBackoffCap{30000};
constexpr std::chrono::milliseconds kStopPoll{200};

std::string build_stream_path(const std::vector<std::string>& symbolsUpper,
                              const std::string& intervalLabel) {
    std::ostringstream oss;
    oss << kBasePath;
    bool first = true;
    for (const auto& symbolUpper : symbolsUpper) {
        if (!first) {
            oss << '/';
        }
        first = false;
        std::string lower = symbolUpper;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        oss << lower << "@kline_" << intervalLabel;
    }
    return oss.str();
}

std::runtime_error make_error(const std::string& message) {
    return std::runtime_error("BinanceWsClient: " + message);
}

}  // namespace

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

BinanceWsClient::WorkGuard::WorkGuard(net::io_context& ctx)
#if __has_include(<boost/asio/executor_work_guard.hpp>)
    : guard_(ctx.get_executor())
#else
    : work_(std::make_unique<boost::asio::io_context::work>(ctx))
#endif
{
}

void BinanceWsClient::WorkGuard::reset()
{
#if __has_include(<boost/asio/executor_work_guard.hpp>)
    guard_.reset();
#else
    work_.reset();
#endif
}

BinanceWsClient::BinanceWsClient() = default;

BinanceWsClient::~BinanceWsClient() {
    stop();
}

void BinanceWsClient::subscribe(const std::vector<std::string>& symbols,
                                domain::Interval interval,
                                std::function<void(const std::string&, const domain::Candle&)> on_closed_candle) {
    if (symbols.empty()) {
        throw make_error("subscribe called with empty symbol list");
    }
    if (!interval.valid() || interval.ms != 60'000) {
        throw make_error("only 1m interval supported for live klines");
    }
    if (!on_closed_candle) {
        throw make_error("subscribe requires a valid callback");
    }

    std::vector<std::string> normalized;
    normalized.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        auto upper = normalize_symbol_(symbol);
        if (upper.empty()) {
            throw make_error("symbol cannot be empty");
        }
        normalized.push_back(std::move(upper));
    }

    const std::string intervalLabel = domain::interval_label(interval);
    if (intervalLabel.empty()) {
        throw make_error("unsupported interval for live klines");
    }

    bool expected = false;
    if (!subscribed_.compare_exchange_strong(expected, true)) {
        throw make_error("already subscribed");
    }

    running_.store(true, std::memory_order_release);
    last_msg_tp_.store(std::chrono::steady_clock::now(), std::memory_order_release);
    ttp::common::metrics::Registry::instance().setGauge("ws_state", 0.0);
    ttp::common::metrics::Registry::instance().setGauge("last_msg_age_ms", 0.0);
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        on_closed_candle_ = std::move(on_closed_candle);
    }

    LOG_INFO(logging::LogCategory::NET,
             "BinanceWsClient subscribe requested symbols=%zu interval_ms=%lld",
             normalized.size(),
             static_cast<long long>(interval.ms));

    worker_ = std::thread(&BinanceWsClient::run_, this, std::move(normalized), interval);
}

void BinanceWsClient::set_on_reconnected(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_reconnected_ = std::move(callback);
}

void BinanceWsClient::stop() {
    running_.store(false, std::memory_order_release);

    std::shared_ptr<WsStream> ws;
    std::shared_ptr<net::steady_timer> pingTimer;
    std::shared_ptr<net::steady_timer> silenceTimer;
    std::shared_ptr<net::io_context> ioc;
    std::shared_ptr<WorkGuard> work;
    std::shared_ptr<std::thread> ioThread;
    {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        ws = active_ws_;
        pingTimer = ping_timer_;
        silenceTimer = silence_timer_;
        ioc = active_ioc_;
        work = work_guard_;
        ioThread = io_thread_;
    }
    if (pingTimer) {
        beast::error_code ec;
        pingTimer->cancel(ec);
    }
    if (silenceTimer) {
        beast::error_code ec;
        silenceTimer->cancel(ec);
    }
    if (ws) {
        try {
            auto promisePtr = std::make_shared<std::promise<void>>();
            auto future = promisePtr->get_future();
            auto completion = std::make_shared<std::atomic<bool>>(false);
            auto closeTimer = std::make_shared<net::steady_timer>(ws->get_executor());
            const auto timeout = std::chrono::seconds(5);

            net::post(ws->get_executor(), [w = ws,
                                           promisePtr,
                                           completion,
                                           closeTimer,
                                           timeout]() mutable {
                closeTimer->expires_after(timeout);
                closeTimer->async_wait([w, promisePtr, completion](const beast::error_code& ec) mutable {
                    if (!ec) {
                        beast::error_code shutdownEc;
                        if (w->is_open()) {
                            w->close(websocket::close_code::normal, shutdownEc);
                        }
                        w->next_layer().shutdown(shutdownEc);
                        beast::error_code closeEc;
                        beast::get_lowest_layer(*w).socket().close(closeEc);
                    }
                    if (!completion->exchange(true)) {
                        promisePtr->set_value();
                    }
                });

                w->async_close(websocket::close_code::normal,
                               [closeTimer, promisePtr, completion](const beast::error_code&) mutable {
                                   beast::error_code cancelEc;
                                   closeTimer->cancel(cancelEc);
                                   if (!completion->exchange(true)) {
                                       promisePtr->set_value();
                                   }
                               });
            });

            future.wait();
        } catch (...) {
            beast::error_code ec;
            if (ws->is_open()) {
                ws->close(websocket::close_code::normal, ec);
            }
            beast::error_code shutdownEc;
            ws->next_layer().shutdown(shutdownEc);
            beast::error_code closeEc;
            beast::get_lowest_layer(*ws).socket().close(closeEc);
        }
    }

    if (work) {
        work->reset();
    }
    if (ioc) {
        ioc->stop();
    }
    if (ioThread && ioThread->joinable()) {
        ioThread->join();
    }

    {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        if (active_ws_ == ws) {
            active_ws_.reset();
        }
        if (ping_timer_ == pingTimer) {
            ping_timer_.reset();
        }
        if (silence_timer_ == silenceTimer) {
            silence_timer_.reset();
        }
        if (active_ioc_ == ioc) {
            active_ioc_.reset();
        }
        if (work_guard_ == work) {
            work_guard_.reset();
        }
        if (io_thread_ == ioThread) {
            io_thread_.reset();
        }
    }

    if (worker_.joinable()) {
        worker_.join();
    }
    subscribed_.store(false, std::memory_order_release);
    ttp::common::metrics::Registry::instance().setGauge("ws_state", 0.0);
}

void BinanceWsClient::run_(std::vector<std::string> symbolsUpper, domain::Interval interval) {
    try {
        LOG_INFO(logging::LogCategory::NET, "BinanceWsClient worker thread starting");

        std::size_t attempt = 0;
        std::mt19937 rng{std::random_device{}()};
        const std::string intervalLabel = domain::interval_label(interval);
        if (intervalLabel.empty()) {
            throw make_error("unsupported interval for live klines");
        }

        while (running_.load(std::memory_order_acquire)) {
            auto ioc = std::make_shared<net::io_context>();
            ssl::context sslCtx(ssl::context::tls_client);
            sslCtx.set_default_verify_paths();
            sslCtx.set_verify_mode(ssl::verify_peer);

            auto work = std::make_shared<WorkGuard>(*ioc);
            auto ws = std::make_shared<WsStream>(*ioc, sslCtx);
            auto pingTimer = std::make_shared<net::steady_timer>(*ioc);
            auto silenceTimer = std::make_shared<net::steady_timer>(*ioc);
            auto ioThread = std::make_shared<std::thread>([ioc]() { ioc->run(); });

            {
                std::lock_guard<std::mutex> lock(ws_mutex_);
                active_ws_ = ws;
                ping_timer_ = pingTimer;
                silence_timer_ = silenceTimer;
                active_ioc_ = ioc;
                work_guard_ = work;
                io_thread_ = ioThread;
            }

            beast::error_code ec;
            auto cleanup = [this, ws, ioc, work, ioThread, pingTimer, silenceTimer]() mutable {
                if (pingTimer) {
                    beast::error_code cancelEc;
                    pingTimer->cancel(cancelEc);
                }
                if (silenceTimer) {
                    beast::error_code cancelEc;
                    silenceTimer->cancel(cancelEc);
                }
                if (ws) {
                    try {
                        auto promisePtr = std::make_shared<std::promise<void>>();
                        auto future = promisePtr->get_future();
                        net::post(*ioc, [w = ws, promisePtr]() mutable {
                            beast::error_code innerEc;
                            if (w->is_open()) {
                                w->close(websocket::close_code::normal, innerEc);
                            }
                            beast::error_code shutdownEc;
                            w->next_layer().shutdown(shutdownEc);
                            beast::error_code closeEc;
                            beast::get_lowest_layer(*w).socket().close(closeEc);
                            promisePtr->set_value();
                        });
                        future.wait();
                    } catch (...) {
                        beast::error_code innerEc;
                        if (ws->is_open()) {
                            ws->close(websocket::close_code::normal, innerEc);
                        }
                        beast::error_code shutdownEc;
                        ws->next_layer().shutdown(shutdownEc);
                        beast::error_code closeEc;
                        beast::get_lowest_layer(*ws).socket().close(closeEc);
                    }
                }

                if (work) {
                    work->reset();
                }
                if (ioc) {
                    ioc->stop();
                }
                if (ioThread && ioThread->joinable()) {
                    ioThread->join();
                }

                {
                    std::lock_guard<std::mutex> lock(ws_mutex_);
                    if (active_ws_ == ws) {
                        active_ws_.reset();
                    }
                    if (ping_timer_ == pingTimer) {
                        ping_timer_.reset();
                    }
                    if (silence_timer_ == silenceTimer) {
                        silence_timer_.reset();
                    }
                    if (active_ioc_ == ioc) {
                        active_ioc_.reset();
                    }
                    if (work_guard_ == work) {
                        work_guard_.reset();
                    }
                    if (io_thread_ == ioThread) {
                        io_thread_.reset();
                    }
                }
            };

            try {
                ws->next_layer().set_verify_mode(ssl::verify_peer);
                ws->next_layer().set_verify_callback(ssl::rfc2818_verification(kHost));

                const char* const sniHost = kHost;
                if (!::SSL_set_tlsext_host_name(ws->next_layer().native_handle(), sniHost)) {
                    const unsigned long err = ::ERR_get_error();
                    const char* reason = err != 0 ? ::ERR_reason_error_string(err) : nullptr;
                    std::ostringstream oss;
                    oss << "Failed to set SNI host name to '" << kHost << "'";
                    if (reason != nullptr) {
                        oss << ": " << reason;
                    }
                    throw make_error(oss.str());
                }

                ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                    req.set(beast::http::field::user_agent, "TheTradingViewer-BinanceWsClient");
                }));

                net::ip::tcp::resolver resolver(*ioc);
                LOG_INFO(logging::LogCategory::NET,
                         "BinanceWsClient resolving host=%s port=%s",
                         kHost,
                         kPort);
                auto results = resolver.resolve(kHost, kPort, ec);
                if (ec) {
                    throw make_error("DNS resolve failed: " + ec.message());
                }

                LOG_INFO(logging::LogCategory::NET,
                         "BinanceWsClient connecting to %s:%s (attempt=%zu)",
                         kHost,
                         kPort,
                         attempt + 1);
                beast::get_lowest_layer(*ws).connect(results, ec);
                if (ec) {
                    throw make_error("connect failed: " + ec.message());
                }

                ws->next_layer().handshake(ssl::stream_base::client, ec);
                if (ec) {
                    throw make_error("TLS handshake failed: " + ec.message());
                }

                const auto target = build_stream_path(symbolsUpper, intervalLabel);
                ws->handshake(std::string{kHost} + ":" + kPort, target, ec);
                if (ec) {
                    throw make_error("WebSocket handshake failed: " + ec.message());
                }

                last_msg_tp_.store(std::chrono::steady_clock::now(), std::memory_order_release);

                auto wsWeak = std::weak_ptr<WsStream>(ws);
                auto close_ws = [wsWeak]() {
                    if (auto locked = wsWeak.lock()) {
                        beast::error_code closeEc;
                        if (locked->is_open()) {
                            locked->close(websocket::close_code::normal, closeEc);
                        }
                    }
                };

                const auto pingInterval = std::chrono::seconds(60);
                const auto silenceInterval = std::chrono::seconds(10);
                const auto silenceThreshold =
                    std::chrono::milliseconds(interval.ms * 2 + 5000);

                auto pingScheduler = std::make_shared<std::function<void()>>();
                auto silenceScheduler = std::make_shared<std::function<void()>>();

                *pingScheduler = [this, wsWeak, pingTimer, pingScheduler, pingInterval, close_ws]() {
                    pingTimer->expires_after(pingInterval);
                    pingTimer->async_wait([this,
                                           wsWeak,
                                           pingTimer,
                                           pingScheduler,
                                           pingInterval,
                                           close_ws](
                                              const beast::error_code& timerEc) {
                        if (timerEc == net::error::operation_aborted ||
                            !running_.load(std::memory_order_acquire)) {
                            return;
                        }
                        if (timerEc) {
                            LOG_WARN(logging::LogCategory::NET,
                                     "BinanceWsClient ping timer error: %s",
                                     timerEc.message().c_str());
                            close_ws();
                            return;
                        }
                        auto wsLocked = wsWeak.lock();
                        if (!wsLocked) {
                            return;
                        }
                        if (!wsLocked->is_open()) {
                            return;
                        }
                        wsLocked->async_ping(websocket::ping_data{},
                                       [this,
                                        wsWeak,
                                        pingTimer,
                                        pingScheduler,
                                        pingInterval,
                                        close_ws](
                                           const beast::error_code& pingEc) {
                                           if (pingEc) {
                                               if (pingEc != net::error::operation_aborted) {
                                                   LOG_WARN(logging::LogCategory::NET,
                                                            "BinanceWsClient ping failed: %s",
                                                            pingEc.message().c_str());
                                               }
                                               close_ws();
                                               return;
                                           }
                                           if (!running_.load(std::memory_order_acquire)) {
                                               return;
                                           }
                                           if (!wsWeak.lock()) {
                                               return;
                                           }
                                           (*pingScheduler)();
                                       });
                    });
                };

                *silenceScheduler = [this,
                                     wsWeak,
                                     silenceTimer,
                                     silenceScheduler,
                                     silenceInterval,
                                     silenceThreshold,
                                     close_ws]() {
                    silenceTimer->expires_after(silenceInterval);
                    silenceTimer->async_wait([this,
                                              wsWeak,
                                              silenceTimer,
                                              silenceScheduler,
                                              silenceThreshold,
                                              close_ws](const beast::error_code& timerEc) {
                        if (timerEc == net::error::operation_aborted ||
                            !running_.load(std::memory_order_acquire)) {
                            return;
                        }
                        if (timerEc) {
                            LOG_WARN(logging::LogCategory::NET,
                                     "BinanceWsClient silence timer error: %s",
                                     timerEc.message().c_str());
                            close_ws();
                            return;
                        }
                        if (auto wsLocked = wsWeak.lock(); !wsLocked || !wsLocked->is_open()) {
                            return;
                        }
                        const auto last = last_msg_tp_.load(std::memory_order_acquire);
                        const auto now = std::chrono::steady_clock::now();
                        if (now - last > silenceThreshold) {
                            LOG_WARN(logging::LogCategory::NET,
                                     "BinanceWsClient silence watchdog triggered");
                            close_ws();
                            return;
                        }
                        if (!running_.load(std::memory_order_acquire)) {
                            return;
                        }
                        (*silenceScheduler)();
                    });
                };

                (*pingScheduler)();
                (*silenceScheduler)();

                {
                    std::lock_guard<std::mutex> lock(ws_mutex_);
                    active_ws_ = ws;
                    ping_timer_ = pingTimer;
                    silence_timer_ = silenceTimer;
                }

                LOG_INFO(logging::LogCategory::NET,
                         "BinanceWsClient connected to %s%s",
                         kHost,
                         target.c_str());

                ttp::common::metrics::Registry::instance().setGauge("ws_state", 1.0);
                ttp::common::metrics::Registry::instance().setGauge("last_msg_age_ms", 0.0);

                std::function<void()> onReconnect;
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    onReconnect = on_reconnected_;
                }

                if (onReconnect) {
                    try {
                        onReconnect();
                    } catch (const std::exception& ex) {
                        LOG_WARN(logging::LogCategory::NET,
                                 "BinanceWsClient on_reconnected callback failed: %s",
                                 ex.what());
                    } catch (...) {
                        LOG_WARN(logging::LogCategory::NET,
                                 "BinanceWsClient on_reconnected callback failed with unknown error");
                    }
                }

                attempt = 0;
                beast::flat_buffer buffer;
                while (running_.load(std::memory_order_acquire)) {
                    buffer.clear();
                    ws->read(buffer, ec);
                    if (ec == websocket::error::closed) {
                        break;
                    }
                    if (ec) {
                        throw make_error("read failed: " + ec.message());
                    }

                    const std::string payload = beast::buffers_to_string(buffer.cdata());
                    if (!payload.empty()) {
                        LOG_DEBUG(logging::LogCategory::NET,
                                  "BinanceWsClient received message bytes=%zu",
                                  payload.size());
                        try {
                            process_message_(payload);
                        } catch (const std::exception& ex) {
                            LOG_WARN(logging::LogCategory::NET,
                                     "BinanceWsClient failed to process message: %s",
                                     ex.what());
                        }
                    }
                }
            } catch (const std::exception& ex) {
                LOG_WARN(logging::LogCategory::NET, "BinanceWsClient connection error: %s", ex.what());
            }

            cleanup();
            ttp::common::metrics::Registry::instance().setGauge("ws_state", 0.0);

            if (!running_.load(std::memory_order_acquire)) {
                break;
            }

            ++attempt;
            ttp::common::metrics::Registry::instance().incrementCounter("reconnect_attempts_total");
            const auto exponent = std::min<std::size_t>(attempt - 1, static_cast<std::size_t>(10));
            const std::uint64_t multiplier = 1ULL << exponent;
            auto backoff = std::chrono::milliseconds(
                kBackoffBase.count() * static_cast<std::int64_t>(multiplier));
            if (backoff > kBackoffCap) {
                backoff = kBackoffCap;
            }
            std::uniform_int_distribution<std::int64_t> jitterDist(0, backoff.count() / 2);
            const auto jitter = std::chrono::milliseconds(jitterDist(rng));
            const auto waitTime = std::min(backoff + jitter, kBackoffCap);

            LOG_INFO(logging::LogCategory::NET,
                     "BinanceWsClient reconnect attempt=%zu wait_ms=%lld",
                     attempt,
                     static_cast<long long>(waitTime.count()));

            auto waited = std::chrono::milliseconds{0};
            while (waited < waitTime && running_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(kStopPoll);
                waited += kStopPoll;
            }
        }

        LOG_INFO(logging::LogCategory::NET, "BinanceWsClient worker thread stopping");
    } catch (const std::exception& ex) {
        LOG_ERROR(logging::LogCategory::NET, "BinanceWsClient worker crashed: %s", ex.what());
    } catch (...) {
        LOG_ERROR(logging::LogCategory::NET, "BinanceWsClient worker crashed: unknown exception");
    }

    running_.store(false, std::memory_order_release);
    subscribed_.store(false, std::memory_order_release);
}

void BinanceWsClient::process_message_(const std::string& payload) {
    boost::json::error_code ec;
    auto json = boost::json::parse(payload, ec);
    if (ec || !json.is_object()) {
        throw make_error("invalid JSON payload");
    }

    const auto& rootObj = json.as_object();
    const auto dataIt = rootObj.if_contains("data");
    if (dataIt == nullptr || !dataIt->is_object()) {
        throw make_error("missing data object");
    }

    const auto& dataObj = dataIt->as_object();
    const auto kIt = dataObj.if_contains("k");
    if (kIt == nullptr || !kIt->is_object()) {
        throw make_error("missing kline object");
    }

    const auto& kObj = kIt->as_object();
    const auto closedIt = kObj.if_contains("x");
    if (closedIt == nullptr || !closedIt->is_bool()) {
        throw make_error("kline missing close flag");
    }
    const bool isClosed = closedIt->as_bool();

    const auto symbolIt = kObj.if_contains("s");
    if (symbolIt == nullptr || !symbolIt->is_string()) {
        throw make_error("kline missing symbol");
    }

    last_msg_tp_.store(std::chrono::steady_clock::now(), std::memory_order_release);
    ttp::common::metrics::Registry::instance().setGauge("last_msg_age_ms", 0.0);

    const auto openTimeIt = kObj.if_contains("t");
    const auto closeTimeIt = kObj.if_contains("T");
    if (openTimeIt == nullptr || closeTimeIt == nullptr) {
        throw make_error("kline missing timestamps");
    }

    domain::Candle candle{};
    candle.openTime = parse_json_int_(*openTimeIt);
    candle.closeTime = parse_json_int_(*closeTimeIt);
    candle.open = parse_json_number_(kObj.at("o"));
    candle.high = parse_json_number_(kObj.at("h"));
    candle.low = parse_json_number_(kObj.at("l"));
    candle.close = parse_json_number_(kObj.at("c"));
    candle.baseVolume = parse_json_number_(kObj.at("v"));
    if (const auto quoteIt = kObj.if_contains("q"); quoteIt != nullptr) {
        candle.quoteVolume = parse_json_number_(*quoteIt);
    }
    if (const auto tradesIt = kObj.if_contains("n"); tradesIt != nullptr) {
        candle.trades = static_cast<domain::TradeCount>(parse_json_int_(*tradesIt));
    }
    candle.isClosed = isClosed;

    const std::string symbolUpper = normalize_symbol_(std::string(symbolIt->as_string().c_str()));

    std::function<void(const std::string&, const domain::Candle&)> callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = on_closed_candle_;
    }
    if (callback) {
        callback(symbolUpper, candle);
    }
}

std::string BinanceWsClient::normalize_symbol_(const std::string& symbol) {
    std::string result;
    result.reserve(symbol.size());
    for (unsigned char ch : symbol) {
        if (!std::isspace(ch)) {
            result.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return result;
}

double BinanceWsClient::parse_json_number_(const boost::json::value& value) {
    if (value.is_double()) {
        return value.as_double();
    }
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }
    if (value.is_string()) {
        const std::string str{value.as_string().c_str()};
        try {
            return std::stod(str);
        } catch (const std::exception& ex) {
            throw make_error("failed to parse floating value: " + std::string(ex.what()));
        }
    }
    throw make_error("unsupported JSON type for double");
}

std::int64_t BinanceWsClient::parse_json_int_(const boost::json::value& value) {
    if (value.is_int64()) {
        return value.as_int64();
    }
    if (value.is_uint64()) {
        return static_cast<std::int64_t>(value.as_uint64());
    }
    if (value.is_double()) {
        return static_cast<std::int64_t>(std::llround(value.as_double()));
    }
    if (value.is_string()) {
        const std::string str{value.as_string().c_str()};
        try {
            return std::stoll(str);
        } catch (const std::exception& ex) {
            throw make_error("failed to parse integer value: " + std::string(ex.what()));
        }
    }
    throw make_error("unsupported JSON type for integer");
}

}  // namespace adapters::binance

