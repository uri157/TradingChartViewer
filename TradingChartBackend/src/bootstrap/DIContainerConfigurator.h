#pragma once

#include "bootstrap/DIContainer.h"
#include "config/Config.h"

#include "core/EventBus.h"
#include "infra/net/WebSocketClient.h"
#include "infra/storage/DatabaseEngine.h"

#if 0
// TODO: legacy-ui
#include "ui/Cursor.h"
#include "ui/Grid.h"
#include "ui/GridTime.h"
#include "ui/GridValues.h"
#include "ui/RenderManager.h"
#include "ui/ResourceProvider.h"
#endif  // legacy-ui

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>

namespace bootstrap {

class DIContainerConfigurator {
public:
    static void configureServices(DIContainer& container) {
        const config::Config& cfg = container.getConfig();
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!cfg.dataDir.empty()) {
            fs::create_directories(cfg.dataDir, ec);
            if (ec) {
                std::fprintf(stderr,
                             "Failed to create data directory %s: %s\n",
                             cfg.dataDir.c_str(),
                             ec.message().c_str());
            }
        }
        ec.clear();
        if (!cfg.cacheDir.empty()) {
            fs::create_directories(cfg.cacheDir, ec);
            if (ec) {
                std::fprintf(stderr,
                             "Failed to create cache directory %s: %s\n",
                             cfg.cacheDir.c_str(),
                             ec.message().c_str());
            }
        }

        container.registerServiceInternal<config::Config>(
            "Config",
            DIContainer::Lifetime::Singleton,
            [&container]() { return new config::Config(container.getConfig()); });

        container.registerServiceInternal<infra::storage::DatabaseEngine>(
            "DatabaseEngine",
            DIContainer::Lifetime::Singleton,
            [&container]() { return new infra::storage::DatabaseEngine(container.getConfig()); });

        container.registerServiceInternal<core::EventBus>(
            "EventBus",
            DIContainer::Lifetime::Singleton,
            std::function<core::EventBus*()>([]() { return new core::EventBus(); }));

        container.registerServiceInternal<infra::net::WebSocketClient>(
            "WebSocketClient",
            DIContainer::Lifetime::Singleton,
            [&container]() {
                const config::Config& cfg = container.getConfig();
                return new infra::net::WebSocketClient(
                    cfg.symbol,
                    cfg.interval,
                    cfg.wsHost,
                    cfg.wsPathTemplate);
            });
#if 0
        // TODO: legacy-ui
        container.registerServiceInternal<ui::ResourceProvider>(
            "ResourceProvider",
            DIContainer::Lifetime::Singleton,
            std::function<ui::ResourceProvider*()>([]() { return new ui::ResourceProvider(); }));

        container.registerServiceInternal<ui::RenderManager>(
            "RenderManager",
            DIContainer::Lifetime::Singleton,
            std::function<ui::RenderManager*()>([]() { return new ui::RenderManager(); }));

        container.registerServiceInternal<sf::RenderWindow>(
            "RenderWindow",
            DIContainer::Lifetime::Singleton,
            [&container]() {
                const config::Config& cfg = container.getConfig();
                sf::Uint32 style = cfg.windowFullscreen ? sf::Style::Fullscreen : sf::Style::Default;
                sf::VideoMode mode;
                if (cfg.windowFullscreen) {
                    mode = sf::VideoMode::getDesktopMode();
                }
                else {
                    unsigned int width = static_cast<unsigned int>(std::max(cfg.windowWidth, 1));
                    unsigned int height = static_cast<unsigned int>(std::max(cfg.windowHeight, 1));
                    mode = sf::VideoMode(width, height);
                }
                auto window = new sf::RenderWindow(mode, "The Trading Project", style);
                if (!cfg.windowFullscreen) {
                    window->setSize(sf::Vector2u(static_cast<unsigned int>(cfg.windowWidth),
                                                 static_cast<unsigned int>(cfg.windowHeight)));
                }
                return window;
            });

        container.registerServiceInternal<ui::Cursor>(
            "Cursor",
            DIContainer::Lifetime::Singleton,
            []() { return new ui::Cursor(); });

        container.registerServiceInternal<ui::Grid>(
            "Grid",
            DIContainer::Lifetime::Scoped,
            [&container]() {
                int scopeId = DIContainer::generateScopeId();
                container.registerServiceInternal<ui::GridTime>(
                    "GridTime",
                    DIContainer::Lifetime::Scoped,
                    [scopeId]() { return new ui::GridTime(scopeId); });

                container.registerServiceInternal<ui::GridValues>(
                    "GridValues",
                    DIContainer::Lifetime::Scoped,
                    [scopeId]() { return new ui::GridValues(scopeId); });

                return new ui::Grid(scopeId);
            });
#endif  // legacy-ui
    }
};

}  // namespace bootstrap

