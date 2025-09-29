// WebSocketClient.h
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "domain/Types.h"
#include "infra/storage/PriceData.h"

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
    std::string symbol_;
    std::string interval_;
    std::string host_;
    std::string pathTemplate_;
    std::function<void(const infra::storage::PriceData&)> dataHandler_;
    std::atomic<bool> connected_{false};
    std::atomic<domain::TimestampMs> lastTickMs_{0};
    std::mutex mutex_;
};

}  // namespace infra::net
