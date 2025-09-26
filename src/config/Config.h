#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace config {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

inline int logLevelSeverity(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return 0;
    case LogLevel::Debug:
        return 1;
    case LogLevel::Info:
        return 2;
    case LogLevel::Warn:
        return 3;
    case LogLevel::Error:
        return 4;
    }
    return 2;
}

inline bool logLevelAtLeast(LogLevel level, LogLevel threshold) {
    return logLevelSeverity(level) <= logLevelSeverity(threshold);
}

struct Config {
    // trading
    std::string symbol           = "BTCUSDT";
    std::string interval         = "1m";

    // IO / paths
    std::string dataDir          = "./data";
    std::string cacheDir         = "./cache";
    std::string configFile       = "";

    // network
    std::string restHost         = "api.binance.com";
    std::string wsHost           = "stream.binance.com";
    std::string wsPathTemplate   = "/ws/%s@kline_%s";

    // UI
    int windowWidth              = 1280;
    int windowHeight             = 720;
    bool windowFullscreen        = false;

    // logs
    LogLevel logLevel            = LogLevel::Info;

    // util
    bool showHelp                = false;
    bool showVersion             = false;
};

}  // namespace config

