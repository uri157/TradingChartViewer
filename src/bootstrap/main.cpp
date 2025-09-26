#include "app/Application.h"
#include "config/ConfigProvider.h"
#include "bootstrap/DIContainer.h"
#include "bootstrap/DIContainerConfigurator.h"
#include "infra/storage/DatabaseEngine.h"
#include "logging/Log.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace bootstrap {
namespace {
void printHelp(const config::Config& defaults) {
    std::cout << "Uso: ttp [opciones]\n"
              << "  -s, --symbol SYMBOL         (default: " << defaults.symbol << ")  Ej: ETHUSDT\n"
              << "  -i, --interval INTERVAL     (default: " << defaults.interval << ")       Ej: 1m, 5m, 1h\n"
              << "  -d, --data-dir PATH         (default: " << defaults.dataDir << ")\n"
              << "  -c, --cache-dir PATH        (default: " << defaults.cacheDir << ")\n"
              << "      --config FILE           Archivo key=value (symbol=..., interval=..., etc.)\n"
              << "  -w, --window-width N        (default: " << defaults.windowWidth << ")\n"
              << "  -h, --window-height N       (default: " << defaults.windowHeight << ")\n"
              << "  -f, --fullscreen            (default: " << (defaults.windowFullscreen ? "true" : "false") << ")\n"
              << "  -l, --log-level LEVEL       trace|debug|info|warn|error (default: "
              << config::ConfigProvider::logLevelToString(defaults.logLevel) << ")\n"
              << "      --rest-host HOST        (default: " << defaults.restHost << ")\n"
              << "      --ws-host HOST          (default: " << defaults.wsHost << ")\n"
              << "      --ws-path TEMPLATE      (default: " << defaults.wsPathTemplate << ")\n"
              << "      --help                  Muestra esta ayuda\n"
              << "      --version               Muestra la versión\n"
              << "Variables de entorno: TTP_SYMBOL, TTP_INTERVAL, TTP_DATA_DIR, TTP_CACHE_DIR, TTP_CONFIG,\n"
              << "                      TTP_WINDOW_W, TTP_WINDOW_H, TTP_FULLSCREEN, TTP_LOG_LEVEL,\n"
              << "                      TTP_REST_HOST, TTP_WS_HOST, TTP_WS_PATH\n"
              << "Precedencia: CLI > ENV > archivo > defaults\n";
}

void printVersion() {
#ifdef PROJECT_NAME
    std::cout << PROJECT_NAME << '\n';
#else
    std::cout << "ttp" << '\n';
#endif
}
}  // namespace

int run(int argc, char** argv) {
    config::ConfigProvider provider(argc, argv);
    const config::Config& config = provider.get();

    if (config.showHelp) {
        printHelp(config::Config{});
        return 0;
    }

    if (config.showVersion) {
        printVersion();
        return 0;
    }

    bool diagnostics = false;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) {
            continue;
        }
        if (std::strcmp(argv[i], "--diag") == 0 || std::strcmp(argv[i], "--diagnostics") == 0) {
            diagnostics = true;
        }
    }

    logging::Log::SetGlobalLogLevel(config.logLevel);

#if defined(TTP_WITH_ASAN)
    constexpr const char* kAsanState = "on";
#else
    constexpr const char* kAsanState = "off";
#endif
#if defined(TTP_WITH_UBSAN)
    constexpr const char* kUbsanState = "on";
#else
    constexpr const char* kUbsanState = "off";
#endif

    LOG_INFO(logging::LogCategory::UI,
             "Startup level=%s ASAN=%s UBSAN=%s",
             logging::Log::level_to_string(config.logLevel),
             kAsanState,
             kUbsanState);

    bool enableTracing = config::logLevelAtLeast(config.logLevel, config::LogLevel::Debug);
    if (const char* debugEnv = std::getenv("TTP_DEBUG")) {
        if (std::strcmp(debugEnv, "0") != 0) {
            enableTracing = true;
        }
    }
    infra::storage::DatabaseEngine::setGlobalTracing(enableTracing);

    if (diagnostics) {
        infra::storage::DatabaseEngine db(config);
        db.printDiagnostics(std::cout);
        return 0;
    }

    bootstrap::DIContainer container;
    container.setConfig(config);
    bootstrap::DIContainer::setGlobalInstance(&container);
    bootstrap::DIContainerConfigurator::configureServices(container);

    app::Application app(config);
    app.run();

    return 0;
}

}  // namespace bootstrap

int main(int argc, char** argv) {
    return bootstrap::run(argc, argv);
}

