#include "config/ConfigProvider.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace config {

namespace {
constexpr int kMinWindowSize = 320;
}  // namespace

ConfigProvider::ConfigProvider(int argc, const char* const* argv) {
    std::string cliConfigPath;
    for (int i = 1; i < argc; ++i) {
        const char* raw = argv[i];
        if (!raw) {
            continue;
        }
        std::string arg(raw);
        if (arg == "--config") {
            if (i + 1 >= argc || !argv[i + 1]) {
                std::fprintf(stderr, "Missing value for --config\n");
            }
            else {
                cliConfigPath = argv[++i];
            }
        }
        else if (arg.rfind("--config=", 0) == 0) {
            cliConfigPath = arg.substr(9);
        }
    }

    if (!cliConfigPath.empty()) {
        if (fileExists_(cliConfigPath)) {
            parseFile_(cliConfigPath);
            cfg_.configFile = cliConfigPath;
        }
        else {
            std::fprintf(stderr, "Config file not found: %s\n", cliConfigPath.c_str());
        }
    }
    else if (const char* envCfg = std::getenv("TTP_CONFIG")) {
        std::string path(envCfg);
        if (fileExists_(path)) {
            parseFile_(path);
            cfg_.configFile = path;
        }
        else {
            std::fprintf(stderr, "Config file not found: %s\n", path.c_str());
        }
    }

    parseEnv_();
    parseCli_(argc, argv);
}

LogLevel ConfigProvider::parseLogLevel(const std::string& value) {
    std::string lower = lowercase_(value);
    if (lower == "trace") {
        return LogLevel::Trace;
    }
    if (lower == "debug") {
        return LogLevel::Debug;
    }
    if (lower == "info") {
        return LogLevel::Info;
    }
    if (lower == "warn" || lower == "warning") {
        return LogLevel::Warn;
    }
    if (lower == "error") {
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

std::string ConfigProvider::logLevelToString(LogLevel l) {
    switch (l) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warn:
        return "warn";
    case LogLevel::Error:
        return "error";
    }
    return "info";
}

void ConfigProvider::parseCli_(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        const char* raw = argv[i];
        if (!raw) {
            continue;
        }
        std::string arg(raw);

        auto takeNext = [&](const char* name) -> std::optional<std::string> {
            if (i + 1 >= argc || !argv[i + 1]) {
                std::fprintf(stderr, "Missing value for %s\n", name);
                return std::nullopt;
            }
            ++i;
            return std::string(argv[i]);
        };

        if (arg == "--help") {
            cfg_.showHelp = true;
        }
        else if (arg == "--version") {
            cfg_.showVersion = true;
        }
        else if (arg == "--symbol" || arg == "-s") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.symbol = *next;
            }
        }
        else if (arg == "--interval" || arg == "-i") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.interval = *next;
            }
        }
        else if (arg == "--backfill-mode") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.backfillMode = *next;
            }
        }
        else if (arg == "--lookback-max") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.lookbackMax = *next;
            }
        }
        else if (arg == "--backfill-chunk") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value) && value > 0) {
                    cfg_.backfillChunk = static_cast<std::size_t>(value);
                }
            }
        }
        else if (arg == "--backfill-concurrency") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value) && value > 0) {
                    cfg_.backfillConcurrency = static_cast<std::size_t>(value);
                }
            }
        }
        else if (arg == "--ws-warmup") {
            if (auto next = takeNext(arg.c_str())) {
                bool flag{};
                if (parseBool_(*next, flag)) {
                    cfg_.wsWarmup = flag;
                }
            }
        }
        else if (arg == "--data-dir" || arg == "-d") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.dataDir = *next;
            }
        }
        else if (arg == "--cache-dir" || arg == "-c") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.cacheDir = *next;
            }
        }
        else if (arg == "--config") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.configFile = *next;
            }
        }
        else if (arg.rfind("--config=", 0) == 0) {
            cfg_.configFile = arg.substr(9);
        }
        else if (arg == "--window-width" || arg == "-w") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value)) {
                    cfg_.windowWidth = std::max(value, kMinWindowSize);
                }
            }
        }
        else if (arg == "--window-height" || arg == "-h") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value)) {
                    cfg_.windowHeight = std::max(value, kMinWindowSize);
                }
            }
        }
        else if (arg == "--right-axis-width") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value) && value >= 0) {
                    cfg_.rightAxisWidth = value;
                }
            }
        }
        else if (arg == "--bottom-axis-height") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value) && value >= 0) {
                    cfg_.bottomAxisHeight = value;
                }
            }
        }
        else if (arg == "--left-sidebar-width") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value) && value >= 0) {
                    cfg_.leftSidebarWidth = value;
                }
            }
        }
        else if (arg == "--top-toolbar-height") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value) && value >= 0) {
                    cfg_.topToolbarHeight = value;
                }
            }
        }
        else if (arg == "--ui-theme") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.uiTheme = lowercase_(*next);
            }
        }
        else if (arg == "--ui-axis-font-size") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value) && value > 0) {
                    cfg_.uiAxisFontSizePx = value;
                }
            }
        }
        else if (arg == "--ui-chart-font-size") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value) && value > 0) {
                    cfg_.uiChartFontSizePx = value;
                }
            }
        }
        else if (arg == "--fullscreen" || arg == "-f") {
            cfg_.windowFullscreen = true;
        }
        else if (arg.rfind("--fullscreen=", 0) == 0) {
            bool value = false;
            if (parseBool_(arg.substr(13), value)) {
                cfg_.windowFullscreen = value;
            }
            else {
                std::fprintf(stderr, "Invalid value for --fullscreen: %s\n", arg.c_str());
            }
        }
        else if (arg == "--log-level" || arg == "-l") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.logLevel = parseLogLevel(*next);
            }
        }
        else if (arg == "--rest-host") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.restHost = *next;
            }
        }
        else if (arg == "--ws-host") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.wsHost = *next;
            }
        }
        else if (arg == "--ws-path") {
            if (auto next = takeNext(arg.c_str())) {
                cfg_.wsPathTemplate = *next;
            }
        }
        else if (arg.rfind("--log-level=", 0) == 0) {
            cfg_.logLevel = parseLogLevel(arg.substr(12));
        }
        else if (arg.rfind("--window-width=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(15), value)) {
                cfg_.windowWidth = std::max(value, kMinWindowSize);
            }
        }
        else if (arg.rfind("--window-height=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(16), value)) {
                cfg_.windowHeight = std::max(value, kMinWindowSize);
            }
        }
        else if (arg.rfind("--right-axis-width=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(19), value) && value >= 0) {
                cfg_.rightAxisWidth = value;
            }
        }
        else if (arg.rfind("--bottom-axis-height=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(21), value) && value >= 0) {
                cfg_.bottomAxisHeight = value;
            }
        }
        else if (arg.rfind("--left-sidebar-width=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(21), value) && value >= 0) {
                cfg_.leftSidebarWidth = value;
            }
        }
        else if (arg.rfind("--top-toolbar-height=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(21), value) && value >= 0) {
                cfg_.topToolbarHeight = value;
            }
        }
        else if (arg.rfind("--data-dir=", 0) == 0) {
            cfg_.dataDir = arg.substr(11);
        }
        else if (arg.rfind("--cache-dir=", 0) == 0) {
            cfg_.cacheDir = arg.substr(12);
        }
        else if (arg.rfind("--symbol=", 0) == 0) {
            cfg_.symbol = arg.substr(9);
        }
        else if (arg.rfind("--interval=", 0) == 0) {
            cfg_.interval = arg.substr(11);
        }
        else if (arg.rfind("--backfill-mode=", 0) == 0) {
            cfg_.backfillMode = arg.substr(16);
        }
        else if (arg.rfind("--lookback-max=", 0) == 0) {
            cfg_.lookbackMax = arg.substr(16);
        }
        else if (arg.rfind("--backfill-chunk=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(18), value) && value > 0) {
                cfg_.backfillChunk = static_cast<std::size_t>(value);
            }
        }
        else if (arg.rfind("--backfill-concurrency=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(24), value) && value > 0) {
                cfg_.backfillConcurrency = static_cast<std::size_t>(value);
            }
        }
        else if (arg == "--publish-candles") {
            if (auto next = takeNext(arg.c_str())) {
                int value{};
                if (parseInt_(*next, value)) {
                    cfg_.publishCandles = static_cast<std::size_t>(std::max(value, 1));
                }
            }
        }
        else if (arg.rfind("--publish-candles=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(18), value)) {
                cfg_.publishCandles = static_cast<std::size_t>(std::max(value, 1));
            }
        }
        else if (arg == "--no-auto-viewport") {
            cfg_.autoViewport = false;
        }
        else if (arg.rfind("--no-auto-viewport=", 0) == 0) {
            bool disable{};
            if (parseBool_(arg.substr(20), disable)) {
                cfg_.autoViewport = !disable;
            }
        }
        else if (arg.rfind("--ws-warmup=", 0) == 0) {
            bool flag{};
            if (parseBool_(arg.substr(12), flag)) {
                cfg_.wsWarmup = flag;
            }
        }
        else if (arg.rfind("--rest-host=", 0) == 0) {
            cfg_.restHost = arg.substr(12);
        }
        else if (arg.rfind("--ws-host=", 0) == 0) {
            cfg_.wsHost = arg.substr(10);
        }
        else if (arg.rfind("--ws-path=", 0) == 0) {
            cfg_.wsPathTemplate = arg.substr(10);
        }
        else if (arg.rfind("--ui-theme=", 0) == 0) {
            cfg_.uiTheme = lowercase_(arg.substr(11));
        }
        else if (arg.rfind("--ui-axis-font-size=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(21), value) && value > 0) {
                cfg_.uiAxisFontSizePx = value;
            }
        }
        else if (arg.rfind("--ui-chart-font-size=", 0) == 0) {
            int value{};
            if (parseInt_(arg.substr(22), value) && value > 0) {
                cfg_.uiChartFontSizePx = value;
            }
        }
    }
}

void ConfigProvider::parseEnv_() {
    if (const char* value = std::getenv("TTP_SYMBOL")) {
        cfg_.symbol = value;
    }
    if (const char* value = std::getenv("TTP_INTERVAL")) {
        cfg_.interval = value;
    }
    if (const char* value = std::getenv("TTP_BACKFILL_MODE")) {
        cfg_.backfillMode = value;
    }
    if (const char* value = std::getenv("TTP_LOOKBACK_MAX")) {
        cfg_.lookbackMax = value;
    }
    if (const char* value = std::getenv("TTP_BACKFILL_CHUNK")) {
        int chunk{};
        if (parseInt_(value, chunk) && chunk > 0) {
            cfg_.backfillChunk = static_cast<std::size_t>(chunk);
        }
    }
    if (const char* value = std::getenv("TTP_BACKFILL_CONCURRENCY")) {
        int workers{};
        if (parseInt_(value, workers) && workers > 0) {
            cfg_.backfillConcurrency = static_cast<std::size_t>(workers);
        }
    }
    if (const char* value = std::getenv("TTP_PUBLISH_CANDLES")) {
        int count{};
        if (parseInt_(value, count)) {
            cfg_.publishCandles = static_cast<std::size_t>(std::max(count, 1));
        }
    }
    if (const char* value = std::getenv("TTP_WS_WARMUP")) {
        bool flag{};
        if (parseBool_(value, flag)) {
            cfg_.wsWarmup = flag;
        }
    }
    if (const char* value = std::getenv("TTP_DATA_DIR")) {
        cfg_.dataDir = value;
    }
    if (const char* value = std::getenv("TTP_CACHE_DIR")) {
        cfg_.cacheDir = value;
    }
    if (const char* value = std::getenv("TTP_WINDOW_W")) {
        int width{};
        if (parseInt_(value, width)) {
            cfg_.windowWidth = std::max(width, kMinWindowSize);
        }
    }
    if (const char* value = std::getenv("TTP_WINDOW_H")) {
        int height{};
        if (parseInt_(value, height)) {
            cfg_.windowHeight = std::max(height, kMinWindowSize);
        }
    }
    if (const char* value = std::getenv("TTP_FULLSCREEN")) {
        bool flag{};
        if (parseBool_(value, flag)) {
            cfg_.windowFullscreen = flag;
        }
    }
    if (const char* value = std::getenv("TTP_LOG_LEVEL")) {
        cfg_.logLevel = parseLogLevel(value);
    }
    if (const char* value = std::getenv("TTP_REST_HOST")) {
        cfg_.restHost = value;
    }
    if (const char* value = std::getenv("TTP_WS_HOST")) {
        cfg_.wsHost = value;
    }
    if (const char* value = std::getenv("TTP_WS_PATH")) {
        cfg_.wsPathTemplate = value;
    }
    if (const char* value = std::getenv("TTP_RIGHT_AXIS_W")) {
        int v{};
        if (parseInt_(value, v) && v >= 0) {
            cfg_.rightAxisWidth = v;
        }
    }
    if (const char* value = std::getenv("TTP_BOTTOM_AXIS_H")) {
        int v{};
        if (parseInt_(value, v) && v >= 0) {
            cfg_.bottomAxisHeight = v;
        }
    }
    if (const char* value = std::getenv("TTP_LEFT_SIDEBAR_W")) {
        int v{};
        if (parseInt_(value, v) && v >= 0) {
            cfg_.leftSidebarWidth = v;
        }
    }
    if (const char* value = std::getenv("TTP_TOP_TOOLBAR_H")) {
        int v{};
        if (parseInt_(value, v) && v >= 0) {
            cfg_.topToolbarHeight = v;
        }
    }
    if (const char* value = std::getenv("TTP_UI_THEME")) {
        cfg_.uiTheme = lowercase_(value);
    }
    if (const char* value = std::getenv("TTP_UI_FONT_AXIS")) {
        int v{};
        if (parseInt_(value, v) && v > 0) {
            cfg_.uiAxisFontSizePx = v;
        }
    }
    if (const char* value = std::getenv("TTP_UI_FONT_CHART")) {
        int v{};
        if (parseInt_(value, v) && v > 0) {
            cfg_.uiChartFontSizePx = v;
        }
    }
    if (const char* value = std::getenv("TTP_NO_AUTO_VIEWPORT")) {
        bool disable{};
        if (parseBool_(value, disable)) {
            cfg_.autoViewport = !disable;
        }
    }
}

void ConfigProvider::parseFile_(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        std::fprintf(stderr, "Unable to open config file: %s\n", path.c_str());
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim_(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = trim_(line.substr(0, pos));
        std::string value = trim_(line.substr(pos + 1));

        if (key == "symbol") {
            cfg_.symbol = value;
        }
        else if (key == "interval") {
            cfg_.interval = value;
        }
        else if (key == "dataDir") {
            cfg_.dataDir = value;
        }
        else if (key == "cacheDir") {
            cfg_.cacheDir = value;
        }
        else if (key == "backfillMode") {
            cfg_.backfillMode = value;
        }
        else if (key == "lookbackMax") {
            cfg_.lookbackMax = value;
        }
        else if (key == "backfillChunk") {
            int chunk{};
            if (parseInt_(value, chunk) && chunk > 0) {
                cfg_.backfillChunk = static_cast<std::size_t>(chunk);
            }
        }
        else if (key == "backfillConcurrency") {
            int workers{};
            if (parseInt_(value, workers) && workers > 0) {
                cfg_.backfillConcurrency = static_cast<std::size_t>(workers);
            }
        }
        else if (key == "wsWarmup") {
            bool flag{};
            if (parseBool_(value, flag)) {
                cfg_.wsWarmup = flag;
            }
        }
        else if (key == "restHost") {
            cfg_.restHost = value;
        }
        else if (key == "wsHost") {
            cfg_.wsHost = value;
        }
        else if (key == "windowWidth") {
            int width{};
            if (parseInt_(value, width)) {
                cfg_.windowWidth = std::max(width, kMinWindowSize);
            }
        }
        else if (key == "windowHeight") {
            int height{};
            if (parseInt_(value, height)) {
                cfg_.windowHeight = std::max(height, kMinWindowSize);
            }
        }
        else if (key == "fullscreen") {
            bool flag{};
            if (parseBool_(value, flag)) {
                cfg_.windowFullscreen = flag;
            }
        }
        else if (key == "logLevel") {
            cfg_.logLevel = parseLogLevel(value);
        }
        else if (key == "wsPath" || key == "wsPathTemplate") {
            cfg_.wsPathTemplate = value;
        }
        else if (key == "rightAxisWidth") {
            int v{};
            if (parseInt_(value, v) && v >= 0) {
                cfg_.rightAxisWidth = v;
            }
        }
        else if (key == "bottomAxisHeight") {
            int v{};
            if (parseInt_(value, v) && v >= 0) {
                cfg_.bottomAxisHeight = v;
            }
        }
        else if (key == "leftSidebarWidth") {
            int v{};
            if (parseInt_(value, v) && v >= 0) {
                cfg_.leftSidebarWidth = v;
            }
        }
        else if (key == "topToolbarHeight") {
            int v{};
            if (parseInt_(value, v) && v >= 0) {
                cfg_.topToolbarHeight = v;
            }
        }
        else if (key == "uiTheme") {
            cfg_.uiTheme = lowercase_(value);
        }
        else if (key == "uiAxisFontSize") {
            int v{};
            if (parseInt_(value, v) && v > 0) {
                cfg_.uiAxisFontSizePx = v;
            }
        }
        else if (key == "uiChartFontSize") {
            int v{};
            if (parseInt_(value, v) && v > 0) {
                cfg_.uiChartFontSizePx = v;
            }
        }
        else if (key == "publishCandles") {
            int v{};
            if (parseInt_(value, v)) {
                cfg_.publishCandles = static_cast<std::size_t>(std::max(v, 1));
            }
        }
        else if (key == "autoViewport") {
            bool flag{};
            if (parseBool_(value, flag)) {
                cfg_.autoViewport = flag;
            }
        }
        else if (key == "noAutoViewport") {
            bool flag{};
            if (parseBool_(value, flag)) {
                cfg_.autoViewport = !flag;
            }
        }
    }
}

bool ConfigProvider::fileExists_(const std::string& path) {
    std::ifstream input(path);
    return input.good();
}

std::string ConfigProvider::trim_(const std::string& s) {
    std::string::size_type start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        ++start;
    }
    std::string::size_type end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(start, end - start);
}

bool ConfigProvider::parseBool_(const std::string& value, bool& out) {
    std::string lower = lowercase_(value);
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        out = true;
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        out = false;
        return true;
    }
    return false;
}

bool ConfigProvider::parseInt_(const std::string& value, int& out) {
    try {
        std::size_t consumed = 0;
        int parsed = std::stoi(value, &consumed, 10);
        if (consumed != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    }
    catch (...) {
        std::fprintf(stderr, "Invalid integer value: %s\n", value.c_str());
        return false;
    }
}

std::string ConfigProvider::lowercase_(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

}  // namespace config

