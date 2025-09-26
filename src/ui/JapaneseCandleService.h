#pragma once

#include <optional>
#include <string>

#include <SFML/Graphics.hpp>
#include <SFML/Window/Event.hpp>

#include "core/EventBus.h"
#include "core/ICacheObserver.h"
#include "core/IFullDataObserver.h"
#include "core/Timestamp.h"
#include "infra/storage/DatabaseEngine.h"
#include "infra/storage/PriceData.h"
#include "ui/GridTime.h"
#include "ui/GridValues.h"
#include "ui/JapaneseCandle.h"
#include "ui/RenderManager.h"

namespace ui {

class JapaneseCandleToolTipService;

class JapaneseCandleService : public core::ICacheObserver, public core::IFullDataObserver {
public:
    JapaneseCandleService(int scopeId, core::Timestamp initialTimestamp);

    void initializeJapaneseCandle(long long timestamp);
    void setOHLC(const infra::storage::DatabaseEngine::OHLC& data);
    void setPriceData(const infra::storage::PriceData& data);
    void actualize();
    void UIEventHandler(sf::Event e);

    // Métodos de las interfaces
    void onCacheUpdated(const infra::storage::PriceData& priceData) override;
    void onFullDataUpdated(const infra::storage::PriceData& priceData) override;

    // Suscripciones
    void subscribeToCache(int cacheIndex);
    void unsubscribeFromCache();
    void subscribeToFullData();
    void unsubscribeFromFullData();

    // Getters
    std::string                 getDateTime();
    std::optional<double>       getMaxPrice() const;
    std::optional<double>       getMinPrice() const;
    std::optional<double>       getOpenPrice() const;
    std::optional<double>       getClosePrice() const;
    float                       getPosX();
    const sf::RectangleShape&   getBody() const;
    bool                        isReady() const;
    long long                   getTimestamp() const;

    void draw();

    JapaneseCandle& operator+=(int n);
    JapaneseCandle& operator-=(int n);

    ~JapaneseCandleService();

    // Dependencias (inyectadas/resueltas en el .cpp)
    int                                 scopeId_ = 0;
    infra::storage::DatabaseEngine*     db = nullptr;
    ui::RenderManager*                  renderManager = nullptr;
    ui::GridTime*                       gridTime = nullptr;
    ui::GridValues*                     gridValues = nullptr;
    sf::RenderWindow*                   w = nullptr;

    JapaneseCandle                      candle;
    // JapaneseCandleToolTipService       candleToolTipService;

    core::EventBus*                     eventBus = nullptr;
    core::EventBus::CallbackID          uiEventHandlerToken{};

    bool subscribedToCache    = false;
    bool subscribedToFullData = false;
    bool showToolTip          = false;

private:
    void centerBodyAndWick();
    void configureClosePriceText(ui::GridValues& gridValues);
};

}  // namespace ui
