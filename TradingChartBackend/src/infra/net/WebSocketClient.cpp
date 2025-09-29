#include "infra/net/WebSocketClient.h"

#include <utility>

#include "logging/Log.h"

namespace infra::net {

WebSocketClient::WebSocketClient(const std::string& symbol,
                                 const std::string& interval,
                                 const std::string& host,
                                 const std::string& pathTemplate)
    : symbol_{symbol}, interval_{interval}, host_{host}, pathTemplate_{pathTemplate} {}

WebSocketClient::~WebSocketClient() {
    disconnect();
}

void WebSocketClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
        return;
    }
    connected_ = true;
    LOG_WARN(::logging::LogCategory::NET,
             "WebSocketClient stub connected without real network support. TODO: restore Boost-based implementation.");
}

void WebSocketClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) {
        return;
    }
    connected_ = false;
    LOG_WARN(::logging::LogCategory::NET,
             "WebSocketClient stub disconnected. TODO: restore Boost-based implementation.");
}

void WebSocketClient::setDataHandler(const std::function<void(const infra::storage::PriceData&)>& handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    dataHandler_ = handler;
}

bool WebSocketClient::isWsConnected() const noexcept {
    return connected_.load();
}

domain::TimestampMs WebSocketClient::lastTickMs() const noexcept {
    return lastTickMs_.load();
}

}  // namespace infra::net

