#include "bootstrap/Config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

namespace {

using bootstrap::HybridConfig;

std::string trim(std::string value) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [is_space](unsigned char ch) {
                     return !is_space(ch);
                 }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [is_space](unsigned char ch) {
                     return !is_space(ch);
                 }).base(),
                value.end());
    return value;
}

std::string unquote(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

bool parseInt(const char* text, int& out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }

    char* endPtr = nullptr;
    const long value = std::strtol(text, &endPtr, 10);
    if (endPtr == text || *endPtr != '\0') {
        return false;
    }
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        return false;
    }

    out = static_cast<int>(value);
    return true;
}

bool parseInt(const std::string& text, int& out) {
    return parseInt(text.c_str(), out);
}

void applyKeyValue(const std::string& key, const std::string& value, HybridConfig& config) {
    if (key == "db_path") {
        config.db_path = value;
        return;
    }
    if (key == "rest_port") {
        int parsed = config.rest_port;
        if (parseInt(value, parsed)) {
            config.rest_port = parsed;
        }
        return;
    }
    if (key == "ws_port") {
        int parsed = config.ws_port;
        if (parseInt(value, parsed)) {
            config.ws_port = parsed;
        }
        return;
    }
    if (key == "default_symbol") {
        config.default_symbol = value;
        return;
    }
    if (key == "default_interval") {
        config.default_interval = value;
        return;
    }
}

void applyFile(const std::string& path, HybridConfig& config) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));
        value = unquote(value);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        applyKeyValue(key, value, config);
    }
}

void applyEnv(HybridConfig& config) {
    if (const char* dbPath = std::getenv("TTP_DB_PATH")) {
        if (*dbPath != '\0') {
            config.db_path = dbPath;
        }
    }
    if (const char* restPort = std::getenv("TTP_REST_PORT")) {
        int parsed = config.rest_port;
        if (parseInt(restPort, parsed)) {
            config.rest_port = parsed;
        }
    }
    if (const char* wsPort = std::getenv("TTP_WS_PORT")) {
        int parsed = config.ws_port;
        if (parseInt(wsPort, parsed)) {
            config.ws_port = parsed;
        }
    }
    if (const char* defaultSymbol = std::getenv("TTP_DEFAULT_SYMBOL")) {
        if (*defaultSymbol != '\0') {
            config.default_symbol = defaultSymbol;
        }
    }
    if (const char* defaultInterval = std::getenv("TTP_DEFAULT_INTERVAL")) {
        if (*defaultInterval != '\0') {
            config.default_interval = defaultInterval;
        }
    }
}

}  // namespace

namespace bootstrap {

HybridConfig loadFromEnvOrFile() {
    HybridConfig config{"data/market.duckdb", 8080, 8090, "BTCUSDT", "1m"};

    if (const char* filePath = std::getenv("TTP_HYBRID_CONFIG")) {
        if (filePath[0] != '\0') {
            applyFile(filePath, config);
        }
    }

    applyEnv(config);

    return config;
}

}  // namespace bootstrap

