#include "common/Config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace ttp::common {
namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::uint16_t parsePort(const std::string& value) {
    try {
        const auto portValue = std::stoul(value);
        if (portValue == 0U || portValue > 65535U) {
            throw std::out_of_range("port out of range");
        }
        return static_cast<std::uint16_t>(portValue);
    } catch (const std::exception&) {
        throw std::runtime_error("Puerto inválido: " + value);
    }
}

std::size_t parseThreads(const std::string& value) {
    try {
        const auto parsed = std::stoul(value);
        if (parsed == 0U) {
            throw std::out_of_range("threads must be >= 1");
        }
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Valor de threads inválido: " + value);
    }
}

std::string parseStorage(const std::string& value) {
    const auto normalized = toLower(value);
    if (normalized == "legacy" || normalized == "duck") {
        return normalized;
    }
    throw std::runtime_error("Valor de storage inválido: " + value);
}

std::uint32_t parseDurationMs(const std::string& value, const std::string& label) {
    try {
        const auto parsed = std::stoul(value);
        if (parsed == 0U || parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::out_of_range("duration out of range");
        }
        return static_cast<std::uint32_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Valor inválido para " + label + ": " + value);
    }
}

std::size_t parseSize(const std::string& value, const std::string& label) {
    try {
        const auto parsed = std::stoull(value);
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Valor inválido para " + label + ": " + value);
    }
}

std::int32_t parseHttpLimit(const std::string& value, const std::string& label) {
    try {
        const auto parsed = std::stol(value);
        if (parsed <= 0 || parsed > std::numeric_limits<std::int32_t>::max()) {
            throw std::out_of_range("http limit out of range");
        }
        return static_cast<std::int32_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Valor inválido para " + label + ": " + value);
    }
}

std::string trim(std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](unsigned char ch) {
                   return !isSpace(ch);
               }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](unsigned char ch) {
                    return !isSpace(ch);
                }).base(),
                value.end());
    return value;
}

std::vector<std::string> parseCsvList(const std::string& value) {
    std::vector<std::string> parts;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto trimmed = trim(item);
        if (!trimmed.empty()) {
            parts.push_back(std::move(trimmed));
        }
    }
    return parts;
}

bool parseBool(const std::string& value) {
    const auto normalized = toLower(value);
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        return false;
    }
    throw std::runtime_error("Valor booleano inválido: " + value);
}

std::string valueFromArgs(int argc, char** argv, const std::string& key) {
    const std::string withEquals = key + '=';
    for (int i = 1; i < argc; ++i) {
        std::string arg{argv[i]};
        if (arg == key && i + 1 < argc) {
            return argv[i + 1];
        }
        if (arg.rfind(withEquals, 0) == 0) {
            return arg.substr(withEquals.size());
        }
    }
    return {};
}

bool hasFlag(int argc, char** argv, const std::string& key) {
    for (int i = 1; i < argc; ++i) {
        if (key == argv[i]) {
            return true;
        }
    }
    return false;
}

}  // namespace

Config Config::fromArgs(int argc, char** argv) {
    Config config{};

    if (const char* envPort = std::getenv("PORT")) {
        config.port = parsePort(envPort);
    }
    if (const char* envLogLevel = std::getenv("LOG_LEVEL")) {
        config.logLevel = ttp::log::levelFromString(toLower(envLogLevel));
    }
    if (const char* envPing = std::getenv("WS_PING_PERIOD_MS")) {
        config.wsPingPeriodMs = parseDurationMs(envPing, "WS_PING_PERIOD_MS");
    }
    if (const char* envPong = std::getenv("WS_PONG_TIMEOUT_MS")) {
        config.wsPongTimeoutMs = parseDurationMs(envPong, "WS_PONG_TIMEOUT_MS");
    }
    if (const char* envQueueMsgs = std::getenv("WS_SEND_QUEUE_MAX_MSGS")) {
        config.wsSendQueueMaxMsgs = parseSize(envQueueMsgs, "WS_SEND_QUEUE_MAX_MSGS");
    }
    if (const char* envQueueBytes = std::getenv("WS_SEND_QUEUE_MAX_BYTES")) {
        config.wsSendQueueMaxBytes = parseSize(envQueueBytes, "WS_SEND_QUEUE_MAX_BYTES");
    }
    if (const char* envStall = std::getenv("WS_STALL_TIMEOUT_MS")) {
        config.wsStallTimeoutMs = parseDurationMs(envStall, "WS_STALL_TIMEOUT_MS");
    }
    if (const char* envDefaultLimit = std::getenv("HTTP_DEFAULT_LIMIT")) {
        config.httpDefaultLimit = parseHttpLimit(envDefaultLimit, "HTTP_DEFAULT_LIMIT");
    }
    if (const char* envMaxLimit = std::getenv("HTTP_MAX_LIMIT")) {
        config.httpMaxLimit = parseHttpLimit(envMaxLimit, "HTTP_MAX_LIMIT");
    }
    if (const char* envDuck = std::getenv("DUCKDB_PATH")) {
        auto pathValue = trim(envDuck);
        if (!pathValue.empty()) {
            config.duckdbPath = std::move(pathValue);
        }
    }

    if (auto portArg = valueFromArgs(argc, argv, "--port"); !portArg.empty()) {
        config.port = parsePort(portArg);
    }
    if (auto levelArg = valueFromArgs(argc, argv, "--log-level"); !levelArg.empty()) {
        config.logLevel = ttp::log::levelFromString(toLower(levelArg));
    }
    if (auto threadsArg = valueFromArgs(argc, argv, "--threads"); !threadsArg.empty()) {
        config.threads = parseThreads(threadsArg);
    }
    if (auto storageArg = valueFromArgs(argc, argv, "--storage"); !storageArg.empty()) {
        config.storage = parseStorage(storageArg);
    }
    if (auto duckArg = valueFromArgs(argc, argv, "--duckdb"); !duckArg.empty()) {
        config.duckdbPath = duckArg;
    }
    if (auto pingArg = valueFromArgs(argc, argv, "--ws-ping-period-ms"); !pingArg.empty()) {
        config.wsPingPeriodMs = parseDurationMs(pingArg, "--ws-ping-period-ms");
    }
    if (auto pongArg = valueFromArgs(argc, argv, "--ws-pong-timeout-ms"); !pongArg.empty()) {
        config.wsPongTimeoutMs = parseDurationMs(pongArg, "--ws-pong-timeout-ms");
    }
    if (auto queueMsgsArg = valueFromArgs(argc, argv, "--ws-send-queue-max-msgs"); !queueMsgsArg.empty()) {
        config.wsSendQueueMaxMsgs = parseSize(queueMsgsArg, "--ws-send-queue-max-msgs");
    }
    if (auto queueBytesArg = valueFromArgs(argc, argv, "--ws-send-queue-max-bytes"); !queueBytesArg.empty()) {
        config.wsSendQueueMaxBytes = parseSize(queueBytesArg, "--ws-send-queue-max-bytes");
    }
    if (auto stallArg = valueFromArgs(argc, argv, "--ws-stall-timeout-ms"); !stallArg.empty()) {
        config.wsStallTimeoutMs = parseDurationMs(stallArg, "--ws-stall-timeout-ms");
    }
    if (auto httpDefaultArg = valueFromArgs(argc, argv, "--http-default-limit"); !httpDefaultArg.empty()) {
        config.httpDefaultLimit = parseHttpLimit(httpDefaultArg, "--http-default-limit");
    }
    if (auto httpMaxArg = valueFromArgs(argc, argv, "--http-max-limit"); !httpMaxArg.empty()) {
        config.httpMaxLimit = parseHttpLimit(httpMaxArg, "--http-max-limit");
    }
    if (auto corsEnableArg = valueFromArgs(argc, argv, "--http.cors.enable"); !corsEnableArg.empty()) {
        config.httpCorsEnable = parseBool(corsEnableArg);
    }
    if (auto corsOriginArg = valueFromArgs(argc, argv, "--http.cors.origin"); !corsOriginArg.empty()) {
        config.httpCorsOrigin = trim(corsOriginArg);
    }

    if (hasFlag(argc, argv, "--backfill")) {
        config.backfill = true;
    }

    if (auto exchangeArg = valueFromArgs(argc, argv, "--exchange"); !exchangeArg.empty()) {
        config.backfillExchange = toLower(exchangeArg);
    }
    if (auto symbolsArg = valueFromArgs(argc, argv, "--symbols"); !symbolsArg.empty()) {
        auto list = parseCsvList(symbolsArg);
        if (!list.empty()) {
            config.backfillSymbols = std::move(list);
        }
    }
    if (auto intervalsArg = valueFromArgs(argc, argv, "--intervals"); !intervalsArg.empty()) {
        auto list = parseCsvList(intervalsArg);
        if (!list.empty()) {
            config.backfillIntervals = std::move(list);
        }
    }
    if (auto fromArg = valueFromArgs(argc, argv, "--from"); !fromArg.empty()) {
        config.backfillFrom = trim(fromArg);
    }
    if (auto toArg = valueFromArgs(argc, argv, "--to"); !toArg.empty()) {
        config.backfillTo = trim(toArg);
    }

    if (auto liveArg = valueFromArgs(argc, argv, "--live"); !liveArg.empty()) {
        config.live = parseBool(liveArg);
    }
    if (auto liveSymbolsArg = valueFromArgs(argc, argv, "--live-symbols"); !liveSymbolsArg.empty()) {
        auto list = parseCsvList(liveSymbolsArg);
        if (!list.empty()) {
            config.liveSymbols = std::move(list);
        }
    }
    if (auto liveIntervalsArg = valueFromArgs(argc, argv, "--live-intervals"); !liveIntervalsArg.empty()) {
        auto list = parseCsvList(liveIntervalsArg);
        if (!list.empty()) {
            config.liveIntervals = std::move(list);
        }
    }

    if (config.live) {
        if (config.liveSymbols.empty()) {
            throw std::runtime_error("La opción --live requiere --live-symbols");
        }
        if (config.liveIntervals.empty()) {
            throw std::runtime_error("La opción --live requiere --live-intervals");
        }
        if (config.liveIntervals.size() != 1) {
            throw std::runtime_error("Por ahora solo se soporta un intervalo en --live-intervals");
        }
        auto& intervalLabel = config.liveIntervals.front();
        intervalLabel = toLower(intervalLabel);
        if (intervalLabel != "1m") {
            throw std::runtime_error("Intervalo live no soportado: " + intervalLabel);
        }
    } else {
        config.liveSymbols.clear();
        config.liveIntervals.clear();
    }

    if (config.httpMaxLimit <= 0) {
        config.httpMaxLimit = 5000;
    }
    if (config.httpDefaultLimit <= 0) {
        config.httpDefaultLimit = 600;
    }
    if (config.httpDefaultLimit > config.httpMaxLimit) {
        config.httpDefaultLimit = config.httpMaxLimit;
    }

    const std::filesystem::path duckPath{config.duckdbPath};
    const auto parentDir = duckPath.parent_path();
    if (!parentDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            throw std::runtime_error("No se pudo crear el directorio para DuckDB (" + parentDir.string() + "): " +
                                     ec.message());
        }
    }

    LOG_INFO("DuckDB path: " << duckPath.string());

    return config;
}

}  // namespace ttp::common
