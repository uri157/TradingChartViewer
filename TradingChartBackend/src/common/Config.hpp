#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/Log.hpp"

namespace ttp::common {

struct Config {
    std::uint16_t port = 8080;
    ttp::log::Level logLevel = ttp::log::Level::Info;
    std::size_t threads = 1;
    std::string storage = "legacy";
    std::string duckdbPath = "/data/market.duckdb";
    bool backfill = false;
    std::string backfillExchange = "binance";
    std::vector<std::string> backfillSymbols{"BTCUSDT", "ETHUSDT"};
    std::vector<std::string> backfillIntervals{"1m"};
    std::string backfillFrom = "2025-08-01";
    std::string backfillTo = "now";

    bool live = false;
    std::vector<std::string> liveSymbols{};
    std::vector<std::string> liveIntervals{};

    std::uint32_t wsPingPeriodMs = 30000;
    std::uint32_t wsPongTimeoutMs = 75000;
    std::size_t wsSendQueueMaxMsgs = 500;
    std::size_t wsSendQueueMaxBytes = 15728640;  // 15 MiB
    std::uint32_t wsStallTimeoutMs = 20000;

    std::int32_t httpDefaultLimit = 600;
    std::int32_t httpMaxLimit = 5000;
    bool httpCorsEnable = false;
    std::string httpCorsOrigin;

    static Config fromArgs(int argc, char** argv);
};

}  // namespace ttp::common
