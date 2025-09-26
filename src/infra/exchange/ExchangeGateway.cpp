#include "infra/exchange/ExchangeGateway.h"
#include "logging/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/connect.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/version.hpp>
#include <boost/json.hpp>
#include <openssl/err.h>

namespace infra::exchange {

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace websocket = beast::websocket;
namespace ssl = net::ssl;
namespace json = boost::json;
using tcp = net::ip::tcp;
using Clock = std::chrono::steady_clock;
using WebsocketStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

constexpr double kPriceEpsilon = 1e-9;

double parseDecimal(const json::value& value) {
    const auto& str = value.as_string();
    return std::stod(std::string(str.data(), str.size()));
}

bool validateCandle(const domain::Candle& candle) {
    const auto minPrice = std::min(candle.open, candle.close);
    const auto maxPrice = std::max(candle.open, candle.close);
    if (candle.low - minPrice > kPriceEpsilon) {
        return false;
    }
    if (maxPrice - candle.high > kPriceEpsilon) {
        return false;
    }
    if (candle.low - candle.high > kPriceEpsilon) {
        return false;
    }
    return true;
}

std::size_t computeBackoffMs(const ExchangeGatewayConfig& cfg, std::size_t attempt) {
    if (cfg.backoffBaseMs <= 0) {
        return 0;
    }
    const auto cappedAttempt = std::min<std::size_t>(attempt, 16);
    const auto factor = static_cast<std::size_t>(1) << cappedAttempt;
    const auto raw = static_cast<std::size_t>(cfg.backoffBaseMs) * factor;
    return static_cast<std::size_t>(std::min<std::size_t>(raw, static_cast<std::size_t>(cfg.backoffCapMs > 0 ? cfg.backoffCapMs : raw)));
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string formatWsTarget(const std::string& templ, const std::string& lowerSymbol, const std::string& intervalLabel) {
    if (templ.find("%s") == std::string::npos) {
        std::string base = templ.empty() ? std::string{"/ws"} : templ;
        if (!base.empty() && base.back() != '/') {
            base.push_back('/');
        }
        return base + lowerSymbol + "@kline_" + intervalLabel;
    }

    std::string result;
    result.reserve(templ.size() + lowerSymbol.size() + intervalLabel.size());
    int placeholderIndex = 0;
    for (std::size_t i = 0; i < templ.size();) {
        if (templ[i] == '%' && i + 1 < templ.size() && templ[i + 1] == 's') {
            if (placeholderIndex == 0) {
                result += lowerSymbol;
            }
            else if (placeholderIndex == 1) {
                result += intervalLabel;
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

struct RestPageResult {
    bool ok{false};
    std::vector<domain::Candle> data;
};

RestPageResult fetchRestPage(const ExchangeGatewayConfig& cfg,
                             const std::string& symbol,
                             const std::string& interval,
                             domain::TimestampMs startTime,
                             std::optional<domain::TimestampMs> endTime,
                             std::size_t limit,
                             domain::TimestampMs intervalMs) {
    RestPageResult result;

    net::io_context ioc;
    tcp::resolver resolver{ioc};
    ssl::context sslCtx{ssl::context::tlsv12_client};
    sslCtx.set_default_verify_paths();
    sslCtx.set_verify_mode(ssl::verify_peer);

    try {
        auto const results = resolver.resolve(cfg.restHost, "443");
        ssl::stream<tcp::socket> stream{ioc, sslCtx};

        if (!SSL_set_tlsext_host_name(stream.native_handle(), cfg.restHost.c_str())) {
            beast::error_code sniEc{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{sniEc};
        }

        net::connect(stream.next_layer(), results.begin(), results.end());
        stream.handshake(ssl::stream_base::client);

        std::string path = "/api/v3/klines";
        if (cfg.restHost.find("fapi") != std::string::npos) {
            path = "/fapi/v1/klines";
        } else if (cfg.restHost.find("dapi") != std::string::npos) {
            path = "/dapi/v1/klines";
        }

        std::string target = path + "?symbol=" + symbol + "&interval=" + interval;
        if (startTime > 0) {
            target += "&startTime=" + std::to_string(startTime);
        }
        if (endTime && *endTime > 0) {
            target += "&endTime=" + std::to_string(*endTime);
        }
        if (limit == 0 || limit > cfg.restMaxLimit) {
            limit = cfg.restMaxLimit;
        }
        target += "&limit=" + std::to_string(limit);

        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, cfg.restHost);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);

        if (res.result() != http::status::ok) {
            LOG_WARN(logging::LogCategory::NET,
                     "REST fetch HTTP error status=%d reason=%s",
                     static_cast<int>(res.result_int()),
                     res.reason().data());
            return result;
        }

        const auto body = beast::buffers_to_string(res.body().data());
        auto jsonValue = json::parse(body);
        if (!jsonValue.is_array()) {
            LOG_WARN(logging::LogCategory::NET, "REST fetch unexpected payload");
            return result;
        }

        std::vector<domain::Candle> page;
        page.reserve(jsonValue.as_array().size());

        for (const auto& entry : jsonValue.as_array()) {
            if (!entry.is_array()) {
                continue;
            }
            const auto& arr = entry.as_array();
            if (arr.size() < 12) {
                continue;
            }

            const auto rawOpen = arr[0].as_int64();
            const auto openTime = domain::align_down_ms(rawOpen, intervalMs);
            if (openTime <= 0) {
                continue;
            }

            domain::Candle candle{};
            candle.openTime = openTime;
            candle.closeTime = openTime + intervalMs - 1;
            candle.open = parseDecimal(arr[1]);
            candle.high = parseDecimal(arr[2]);
            candle.low = parseDecimal(arr[3]);
            candle.close = parseDecimal(arr[4]);
            candle.baseVolume = parseDecimal(arr[5]);
            candle.quoteVolume = parseDecimal(arr[7]);
            candle.trades = static_cast<domain::TradeCount>(std::max<std::int64_t>(arr[8].as_int64(), 0));
            candle.isClosed = true;

            if (!validateCandle(candle)) {
                LOG_WARN(logging::LogCategory::NET,
                         "REST candle validation failed open=%lld",
                         static_cast<long long>(candle.openTime));
                continue;
            }

            page.emplace_back(std::move(candle));
        }

        beast::error_code shutdownEc;
        stream.shutdown(shutdownEc);
        if (shutdownEc && shutdownEc != net::error::eof) {
            LOG_DEBUG(logging::LogCategory::NET, "REST shutdown warning: %s", shutdownEc.message().c_str());
        }

        result.ok = true;
        result.data = std::move(page);
    } catch (const std::exception& ex) {
        LOG_WARN(logging::LogCategory::NET, "REST fetch exception: %s", ex.what());
    }

    return result;
}

class WsSubscription;

class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    WsSession(const ExchangeGatewayConfig& cfg,
              domain::Symbol symbol,
              domain::Interval interval,
              std::function<void(const domain::LiveCandle&)> onData,
              std::function<void(const domain::StreamError&)> onError)
        : cfg_(cfg),
          symbol_(std::move(symbol)),
          interval_(std::move(interval)),
          onData_(std::move(onData)),
          onError_(std::move(onError)),
          intervalMs_(interval_.valid() ? interval_.ms : 0),
          intervalLabel_(domain::interval_label(interval_)),
          ioc_(1),
          resolver_(ioc_),
          sslCtx_(ssl::context::tlsv12_client),
          reconnectTimer_(ioc_),
          idleTimer_(ioc_),
          rng_(std::random_device{}()) {
        if (intervalLabel_.empty()) {
            intervalLabel_ = "1m";
        }
        sslCtx_.set_default_verify_paths();
        sslCtx_.set_verify_mode(ssl::verify_peer);
        createStream();
    }

    ~WsSession() {
        stop();
    }

    void start() {
        if (running_.exchange(true)) {
            return;
        }
        ioc_.restart();
        guard_.emplace(ioc_.get_executor());
        thread_ = std::thread([self = shared_from_this()]() { self->ioc_.run(); });
        net::post(ioc_, [self = shared_from_this()]() { self->beginResolve(); });
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        net::post(ioc_, [self = shared_from_this()]() {
            self->reconnectTimer_.cancel();
            self->idleTimer_.cancel();
            if (self->ws_) {
                std::weak_ptr<WsSession> weak = self;
                self->ws_->async_close(websocket::close_code::normal,
                                       [weak](beast::error_code closeEc) {
                                           auto locked = weak.lock();
                                           if (!locked) {
                                               return;
                                           }
                                           if (closeEc && closeEc != net::error::operation_aborted) {
                                               LOG_DEBUG(logging::LogCategory::NET,
                                                         "WS close warning: %s",
                                                         closeEc.message().c_str());
                                           }
                                       });
            }
        });
        if (guard_) {
            guard_->reset();
        }
        ioc_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        guard_.reset();
    }

    void notifyExternalStop() {
        stop();
    }

private:
    void createStream() {
        ws_.reset();
        ws_ = std::make_unique<WebsocketStream>(net::make_strand(ioc_), sslCtx_);
    }

    void beginResolve() {
        if (!running_.load()) {
            return;
        }
        buffer_.consume(buffer_.size());
        auto endpoint = cfg_.wsHost;
        resolver_.async_resolve(endpoint, std::to_string(cfg_.wsPort),
                                beast::bind_front_handler(&WsSession::onResolve, shared_from_this()));
    }

    void onResolve(beast::error_code ec, tcp::resolver::results_type results) {
        if (ec) {
            handleFailure("resolve", ec);
            return;
        }
        if (!ws_) {
            createStream();
        }
        auto& lowest = beast::get_lowest_layer(*ws_);
        lowest.expires_after(std::chrono::seconds(30));
        net::async_connect(lowest.socket(), results.begin(), results.end(),
                           beast::bind_front_handler(&WsSession::onConnect, shared_from_this()));
    }

    void onConnect(beast::error_code ec, tcp::resolver::results_type::iterator) {
        if (ec) {
            handleFailure("connect", ec);
            return;
        }
        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), cfg_.wsHost.c_str())) {
            beast::error_code sniEc{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            handleFailure("sni", sniEc);
            return;
        }
        ws_->next_layer().async_handshake(ssl::stream_base::client,
                                          beast::bind_front_handler(&WsSession::onSslHandshake, shared_from_this()));
    }

    void onSslHandshake(beast::error_code ec) {
        if (ec) {
            handleFailure("ssl_handshake", ec);
            return;
        }
        beast::get_lowest_layer(*ws_).expires_never();
        ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
            req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " ws-client");
        }));

        std::string lowerSymbol = toLower(symbol_);
        std::string target = formatWsTarget(cfg_.wsPathTemplate, lowerSymbol, intervalLabel_);
        ws_->async_handshake(cfg_.wsHost, target,
                             beast::bind_front_handler(&WsSession::onHandshake, shared_from_this()));
    }

    void onHandshake(beast::error_code ec) {
        if (ec) {
            handleFailure("handshake", ec);
            return;
        }
        reconnectAttempts_ = 0;
        LOG_INFO(logging::LogCategory::NET, "WS connected symbol=%s interval=%s", symbol_.c_str(), intervalLabel_.c_str());
        scheduleIdleTimer();
        doRead();
    }

    void doRead() {
        if (!running_.load()) {
            return;
        }
        ws_->async_read(buffer_, beast::bind_front_handler(&WsSession::onRead, shared_from_this()));
    }

    void onRead(beast::error_code ec, std::size_t bytes) {
        boost::ignore_unused(bytes);
        if (ec) {
            if (ec == websocket::error::closed) {
                scheduleReconnect("closed");
                return;
            }
            handleFailure("read", ec);
            return;
        }

        scheduleIdleTimer();

        const auto payload = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        try {
            auto value = json::parse(payload);
            const json::object* root = value.is_object() ? &value.as_object() : nullptr;
            if (!root) {
                return;
            }

            const json::object* payloadObj = nullptr;
            if (const auto* dataValue = root->if_contains("data"); dataValue && dataValue->is_object()) {
                payloadObj = &dataValue->as_object();
            } else {
                payloadObj = root;
            }

            if (!payloadObj) {
                return;
            }

            if (const auto* eventValue = payloadObj->if_contains("e")) {
                auto evt = eventValue->as_string();
                if (evt != "kline") {
                    return;
                }
            }

            const json::object* klineObj = nullptr;
            if (const auto* kValue = payloadObj->if_contains("k"); kValue && kValue->is_object()) {
                klineObj = &kValue->as_object();
            }

            if (!klineObj) {
                return;
            }

            handleKline(*klineObj);
        } catch (const std::exception& ex) {
            LOG_WARN(logging::LogCategory::NET, "WS parse error: %s", ex.what());
        }

        doRead();
    }

    void handleKline(const json::object& kline) {
        if (!kline.if_contains("t") || !kline.if_contains("T")) {
            return;
        }
        const auto rawOpen = kline.at("t").as_int64();
        const auto openTime = domain::align_down_ms(rawOpen, intervalMs_);
        if (openTime <= 0) {
            return;
        }

        domain::Candle candle{};
        candle.openTime = openTime;
        candle.closeTime = openTime + intervalMs_ - 1;
        candle.open = parseDecimal(kline.at("o"));
        candle.high = parseDecimal(kline.at("h"));
        candle.low = parseDecimal(kline.at("l"));
        candle.close = parseDecimal(kline.at("c"));
        candle.baseVolume = parseDecimal(kline.at("v"));
        candle.quoteVolume = parseDecimal(kline.at("q"));
        candle.trades = static_cast<domain::TradeCount>(std::max<std::int64_t>(kline.at("n").as_int64(), 0));
        const bool isClosed = kline.at("x").as_bool();
        candle.isClosed = isClosed;

        if (!validateCandle(candle)) {
            LOG_WARN(logging::LogCategory::NET, "WS candle validation failed open=%lld", static_cast<long long>(candle.openTime));
            return;
        }

        domain::LiveCandle live{};
        live.candle = std::move(candle);
        live.isFinal = isClosed;
        deliver(std::move(live));
    }

    void deliver(domain::LiveCandle live) {
        if (!onData_) {
            return;
        }

        if (live.isFinal) {
            closedBacklog_[live.candle.openTime] = std::move(live);
            flushClosed();
        } else {
            flushClosedUpTo(live.candle.openTime - intervalMs_);
            if (cfg_.trace) {
                LOG_DEBUG(logging::LogCategory::NET,
                          "WS live open=%lld isFinal=%s",
                          static_cast<long long>(live.candle.openTime),
                          live.isFinal ? "true" : "false");
            }
            onData_(live);
        }
    }

    void flushClosed() {
        flushClosedUpTo(std::numeric_limits<domain::TimestampMs>::max());
    }

    void flushClosedUpTo(domain::TimestampMs upTo) {
        auto it = closedBacklog_.begin();
        while (it != closedBacklog_.end() && it->first <= upTo) {
            if (cfg_.trace) {
                LOG_DEBUG(logging::LogCategory::NET,
                          "WS live open=%lld isFinal=%s",
                          static_cast<long long>(it->second.candle.openTime),
                          it->second.isFinal ? "true" : "false");
            }
            if (onData_) {
                onData_(it->second);
            }
            lastClosed_ = it->first;
            it = closedBacklog_.erase(it);
        }
    }

    void handleFailure(const char* stage, beast::error_code ec) {
        if (!running_.load()) {
            return;
        }
        LOG_WARN(logging::LogCategory::NET,
                 "WS error stage=%s message=%s",
                 stage,
                 ec.message().c_str());
        if (onError_) {
            onError_({ec.value(), ec.message()});
        }
        scheduleReconnect(stage);
    }

    void scheduleReconnect(const std::string& reason) {
        if (!running_.load()) {
            return;
        }
        if (ws_) {
            ws_.reset();
        }
        buffer_.consume(buffer_.size());
        ++reconnectAttempts_;
        const auto backoff = computeBackoffMs(cfg_, reconnectAttempts_ - 1);
        const auto jitter = jitterMs(backoff);
        const auto delay = std::chrono::milliseconds(backoff + jitter);
        LOG_WARN(logging::LogCategory::NET,
                 "WS reconnect in %lldms reason=%s",
                 static_cast<long long>(delay.count()),
                 reason.c_str());
        if (onError_) {
            onError_({0, std::string{"reconnecting: "} + reason});
        }
        reconnectTimer_.expires_after(delay);
        reconnectTimer_.async_wait(beast::bind_front_handler(&WsSession::onReconnectTimer, shared_from_this()));
    }

    std::size_t jitterMs(std::size_t base) {
        if (base == 0) {
            return 0;
        }
        std::uniform_int_distribution<std::size_t> dist(0, base / 5 + 1);
        return dist(rng_);
    }

    void onReconnectTimer(beast::error_code ec) {
        if (ec || !running_.load()) {
            return;
        }
        createStream();
        beginResolve();
    }

    void scheduleIdleTimer() {
        if (cfg_.idleTimeoutSec <= 0) {
            return;
        }
        idleTimer_.expires_after(std::chrono::seconds(cfg_.idleTimeoutSec));
        idleTimer_.async_wait(beast::bind_front_handler(&WsSession::onIdleTimeout, shared_from_this()));
    }

    void onIdleTimeout(beast::error_code ec) {
        if (ec || !running_.load()) {
            return;
        }
        LOG_WARN(logging::LogCategory::NET, "WS heartbeat timeout, reconnecting");
        if (onError_) {
            onError_({ec.value(), "heartbeat timeout"});
        }
        scheduleReconnect("timeout");
    }

    ExchangeGatewayConfig cfg_;
    domain::Symbol symbol_;
    domain::Interval interval_;
    std::function<void(const domain::LiveCandle&)> onData_;
    std::function<void(const domain::StreamError&)> onError_;
    domain::TimestampMs intervalMs_;
    std::string intervalLabel_;

    net::io_context ioc_;
    std::optional<net::executor_work_guard<net::io_context::executor_type>> guard_;
    tcp::resolver resolver_;
    ssl::context sslCtx_;
    std::unique_ptr<WebsocketStream> ws_;
    beast::flat_buffer buffer_;
    net::steady_timer reconnectTimer_;
    net::steady_timer idleTimer_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::size_t reconnectAttempts_{0};
    std::map<domain::TimestampMs, domain::LiveCandle> closedBacklog_{};
    domain::TimestampMs lastClosed_{0};
    std::mt19937 rng_;
};

class WsSubscription final : public domain::SubscriptionHandle {
public:
    explicit WsSubscription(std::shared_ptr<WsSession> session)
        : session_(std::move(session)) {}

    ~WsSubscription() override {
        stop();
    }

    void stop() override {
        auto session = session_.lock();
        if (session) {
            session->notifyExternalStop();
            session_.reset();
        }
    }

private:
    std::weak_ptr<WsSession> session_;
};

}  // namespace

ExchangeGateway::ExchangeGateway(ExchangeGatewayConfig cfg) : cfg_(std::move(cfg)) {}

ExchangeGateway::~ExchangeGateway() = default;

std::vector<domain::Candle> ExchangeGateway::fetchRange(const domain::Symbol& symbol,
                                                        const domain::Interval& interval,
                                                        const domain::TimeRange& range,
                                                        std::size_t limit) {
    if (range.empty()) {
        return {};
    }
    if (!interval.valid()) {
        LOG_WARN(logging::LogCategory::NET, "fetchRange invalid interval symbol=%s", symbol.c_str());
        return {};
    }

    const auto intervalMs = interval.ms;
    std::string intervalLabel = domain::interval_label(interval);
    if (intervalLabel.empty()) {
        intervalLabel = "1m";
    }
    const std::size_t effectiveLimit = (limit == 0) ? cfg_.restMaxLimit : std::min(limit, cfg_.restMaxLimit);

    std::map<domain::TimestampMs, domain::Candle> ordered;
    auto cursor = range.start;
    const auto rangeEnd = range.end;

    while (cursor <= rangeEnd) {
        std::optional<domain::TimestampMs> endTime{};
        if (intervalMs > 0 && effectiveLimit > 0) {
            const auto window = intervalMs * static_cast<domain::TimestampMs>(effectiveLimit);
            endTime = std::min(rangeEnd, cursor + window - 1);
        }

        const auto requestEnd = endTime ? *endTime : rangeEnd;
        LOG_INFO(logging::LogCategory::NET,
                 "REST request symbol=%s interval=%s start=%lld end=%lld limit=%zu",
                 symbol.c_str(),
                 intervalLabel.c_str(),
                 static_cast<long long>(cursor),
                 static_cast<long long>(requestEnd),
                 effectiveLimit);

        RestPageResult pageResult;
        bool success = false;
        const int maxAttempts = std::max(cfg_.maxRetries, 0);
        for (int attempt = 0; attempt <= maxAttempts; ++attempt) {
            const auto start = Clock::now();
            pageResult = fetchRestPage(cfg_, symbol, intervalLabel, cursor, endTime, effectiveLimit, intervalMs);
            const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            if (pageResult.ok) {
                success = true;
                if (!pageResult.data.empty()) {
                    LOG_INFO(logging::LogCategory::NET,
                             "REST response symbol=%s interval=%s span=%lld-%lld count=%zu latency_ms=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(pageResult.data.front().openTime),
                             static_cast<long long>(pageResult.data.back().openTime),
                             pageResult.data.size(),
                             static_cast<long long>(latency));
                } else {
                    LOG_INFO(logging::LogCategory::NET,
                             "REST response symbol=%s interval=%s span=empty count=0 latency_ms=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(latency));
                }
                break;
            }

            if (attempt == maxAttempts) {
                break;
            }

            const auto backoff = computeBackoffMs(cfg_, static_cast<std::size_t>(attempt));
            LOG_WARN(logging::LogCategory::NET,
                     "REST retry attempt=%d backoff=%zu", attempt + 1, backoff);
            if (backoff > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
            }
        }

        if (!success) {
            LOG_WARN(logging::LogCategory::NET, "REST aborting fetch after retries symbol=%s", symbol.c_str());
            break;
        }

        if (pageResult.data.empty()) {
            break;
        }

        for (auto& candle : pageResult.data) {
            if (candle.openTime < range.start || candle.openTime > range.end) {
                continue;
            }
            ordered[candle.openTime] = std::move(candle);
        }

        const auto lastFetched = pageResult.data.back().openTime;
        if (lastFetched <= cursor) {
            cursor += intervalMs;
        } else {
            cursor = lastFetched + intervalMs;
        }

        if (pageResult.data.size() < effectiveLimit) {
            break;
        }

        if (cfg_.restMinSleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.restMinSleepMs));
        }
    }

    std::vector<domain::Candle> result;
    result.reserve(ordered.size());
    for (auto& [open, candle] : ordered) {
        result.emplace_back(std::move(candle));
    }
    return result;
}

std::unique_ptr<domain::SubscriptionHandle> ExchangeGateway::streamLive(
    const domain::Symbol& symbol,
    const domain::Interval& interval,
    std::function<void(const domain::LiveCandle&)> onData,
    std::function<void(const domain::StreamError&)> onError) {
    auto session = std::make_shared<WsSession>(cfg_, symbol, interval, std::move(onData), std::move(onError));
    session->start();
    return std::make_unique<WsSubscription>(session);
}

}  // namespace infra::exchange

