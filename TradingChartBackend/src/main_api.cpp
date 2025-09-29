#include <cstdlib>
#include <csignal>
#include <cstdio>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "adapters/binance/BinanceRestClient.hpp"
#include "adapters/binance/BinanceWsClient.hpp"
#include "adapters/duckdb/DuckCandleRepo.hpp"
#include "adapters/duckdb/DuckStore.hpp"
#include "adapters/legacy/LegacyCandleRepo.hpp"
#include "app/BackfillWorker.hpp"
#include "app/LiveIngestor.hpp"
#include "api/Controllers.hpp"
#include "api/HttpServer.hpp"
#include "api/WebSocketServer.hpp"
#include "common/Config.hpp"
#include "common/Log.hpp"

namespace {

volatile std::sig_atomic_t gSignalStatus = 0;

void handleSignal(int signal) {
    gSignalStatus = signal;
}

std::string joinList(const std::vector<std::string>& values) {
    std::string joined;
    for (const auto& value : values) {
        if (joined.empty()) {
            joined = value;
        } else {
            joined.append(",").append(value);
        }
    }
    return joined;
}

}  // namespace

int main(int argc, char** argv) {
    std::set_terminate([] {
        try {
            auto eptr = std::current_exception();
            if (eptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& ex) {
                    std::fprintf(stderr, "std::terminate: %s\n", ex.what());
                } catch (...) {
                    std::fprintf(stderr, "std::terminate: unknown exception\n");
                }
            } else {
                std::fprintf(stderr, "std::terminate without current_exception\n");
            }
        } catch (...) {
        }
        std::_Exit(1);
    });

    try {
        auto config = ttp::common::Config::fromArgs(argc, argv);
        ttp::log::setLevel(config.logLevel);

        LOG_INFO("Configuración cargada");
        LOG_INFO("  Puerto: " << config.port);
        LOG_INFO("  Nivel de log: " << ttp::log::levelToString(config.logLevel));
        LOG_INFO("  Hilos de trabajo: " << config.threads);
        LOG_INFO("  Storage: " << config.storage);
        LOG_INFO("  WS ping period: " << config.wsPingPeriodMs << " ms");
        LOG_INFO("  WS pong timeout: " << config.wsPongTimeoutMs << " ms");
        LOG_INFO("  WS send queue max msgs: " << config.wsSendQueueMaxMsgs);
        LOG_INFO("  WS send queue max bytes: " << config.wsSendQueueMaxBytes);
        LOG_INFO("  WS stall timeout: " << config.wsStallTimeoutMs << " ms");
        LOG_INFO("  HTTP default_limit=" << config.httpDefaultLimit
                 << " max_limit=" << config.httpMaxLimit);

        if (config.backfill && config.storage != "duck") {
            LOG_ERR("La opción --backfill requiere --storage=duck");
            return EXIT_FAILURE;
        }

        std::shared_ptr<adapters::duckdb::DuckCandleRepo> duckRepo;
        std::shared_ptr<const domain::contracts::ICandleReadRepo> repo;
        if (config.storage == "duck") {
#if defined(HAS_DUCKDB)
            try {
                adapters::duckdb::DuckStore store(config.duckdbPath);
                store.migrate();
            } catch (const std::exception& ex) {
                LOG_WARN("No se pudieron aplicar migraciones DuckDB: " << ex.what());
            }

            if (config.backfill) {
                app::BackfillWorker worker(config);
                worker.run();
                LOG_INFO("Backfill finalizado, cerrando proceso");
                return EXIT_SUCCESS;
            }
            duckRepo = std::make_shared<adapters::duckdb::DuckCandleRepo>(config.duckdbPath);
            repo = duckRepo;
            LOG_INFO("Repositorio de velas: DuckDB -> " << config.duckdbPath);
#else
            if (config.backfill) {
                LOG_ERR("Backfill requerido pero DuckDB no está disponible en esta build");
                return EXIT_FAILURE;
            }
            LOG_WARN("DuckDB solicitado pero no disponible en esta build; usando backend legacy");
            repo = std::make_shared<adapters::legacy::LegacyCandleRepo>();
#endif
        } else {
            if (config.backfill) {
                LOG_ERR("Backfill no soportado para storage='" << config.storage << "'");
                return EXIT_FAILURE;
            }
            repo = std::make_shared<adapters::legacy::LegacyCandleRepo>();
            LOG_INFO("Repositorio de velas: Legacy");
        }

        if (config.live && !duckRepo) {
            LOG_ERR("La opción --live requiere --storage=duck y soporte de DuckDB");
            return EXIT_FAILURE;
        }
        ttp::api::setCandleRepository(std::move(repo));
        ttp::api::setHttpLimits(config.httpDefaultLimit, config.httpMaxLimit);
        ttp::api::setLiveSymbols(config.liveSymbols);
        ttp::api::setLiveIntervals(config.liveIntervals);

        ttp::api::IoContext ioContext;
        ttp::api::Endpoint endpoint{"0.0.0.0", config.port};
        ttp::api::HttpServer server(ioContext, endpoint, config.threads);

        ttp::api::HttpServer::CorsConfig corsConfig{};
        corsConfig.enabled = config.httpCorsEnable && !config.httpCorsOrigin.empty();
        corsConfig.origin = config.httpCorsOrigin;
        server.setCorsConfig(std::move(corsConfig));

        ttp::api::WebSocketServer::instance().configureKeepAlive(
            std::chrono::milliseconds(config.wsPingPeriodMs),
            std::chrono::milliseconds(config.wsPongTimeoutMs));
        ttp::api::WebSocketServer::instance().configureBackpressure(
            config.wsSendQueueMaxMsgs,
            config.wsSendQueueMaxBytes,
            std::chrono::milliseconds(config.wsStallTimeoutMs));

        std::unique_ptr<adapters::binance::BinanceRestClient> liveRestClient;
        std::unique_ptr<adapters::binance::BinanceWsClient> liveWsClient;
        std::unique_ptr<app::LiveIngestor> liveIngestor;

        if (config.live) {
            domain::Interval liveInterval{};
            try {
                liveInterval = domain::interval_from_string(config.liveIntervals.front());
            } catch (const std::exception& ex) {
                LOG_ERR("Intervalo live inválido '" << config.liveIntervals.front() << "': " << ex.what());
                return EXIT_FAILURE;
            }

            liveRestClient = std::make_unique<adapters::binance::BinanceRestClient>();
            liveWsClient = std::make_unique<adapters::binance::BinanceWsClient>();
            liveIngestor = std::make_unique<app::LiveIngestor>(*duckRepo, *liveRestClient, *liveWsClient);

            const auto liveSymbols = config.liveSymbols;
            liveIngestor->run(liveSymbols, liveInterval);

            LOG_INFO("Ingesta en vivo habilitada: símbolos="
                     << joinList(config.liveSymbols) << ", intervalo=" << config.liveIntervals.front());
        }

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        server.start();
        LOG_INFO("Servidor en marcha. Esperando solicitudes...");

        while (gSignalStatus == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        LOG_INFO("Señal " << gSignalStatus << " recibida, deteniendo servicios...");
        LOG_INFO("Starting graceful shutdown");

        if (liveIngestor) {
            liveIngestor->stop();
        }

        server.stop();

        server.wait();

        if (liveIngestor) {
            liveIngestor.reset();
        }
        if (liveWsClient) {
            liveWsClient.reset();
        }
        if (liveRestClient) {
            liveRestClient.reset();
        }
        if (duckRepo) {
            ttp::api::setCandleRepository(nullptr);
            duckRepo.reset();
        }

        LOG_INFO("Shutdown complete");
    } catch (const std::exception& ex) {
        LOG_ERR("Error fatal en la API: " << ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
