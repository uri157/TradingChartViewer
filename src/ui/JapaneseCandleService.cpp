
////JapaneseCandleService.cpp
#include "ui/JapaneseCandleService.h"

#include "bootstrap/DIContainer.h"
#include "infra/storage/DatabaseEngine.h"

////JapaneseCandleService.cpp

#include "ui/GridTime.h"
#include "ui/GridValues.h"
#include "ui/JapaneseCandle.h"
#include "ui/RenderManager.h"
#include <cmath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "logging/Log.h"
#include "core/TimeUtils.h"

namespace {
class MissingDataLimiter {
public:
    bool shouldReport(const std::string& label, long long timestamp) {
        std::lock_guard<std::mutex> lock(mutex_);
        return reported_[label].insert(timestamp).second;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_set<long long>> reported_;
};

MissingDataLimiter g_missingDataLimiter;
} // namespace

namespace ui {

JapaneseCandleService::JapaneseCandleService(int scopeId, core::Timestamp initialTimestamp)
    : scopeId_(scopeId),
      db(bootstrap::DIContainer::resolve<infra::storage::DatabaseEngine>("DatabaseEngine")),
      renderManager(bootstrap::DIContainer::resolve<ui::RenderManager>("RenderManager")),
      gridTime(bootstrap::DIContainer::resolve<ui::GridTime>("GridTime", scopeId)),
      gridValues(bootstrap::DIContainer::resolve<ui::GridValues>("GridValues", scopeId)),
      w(bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow")),
      candle(initialTimestamp),
      eventBus(bootstrap::DIContainer::resolve<core::EventBus>("EventBus")) {
                                            // candleToolTipService(*this){
    candle.setDataReady(false);
    uiEventHandlerToken = eventBus->subscribe(sf::Event::MouseMoved, [this](const sf::Event &e) {
        this->UIEventHandler(e);
    });
}

void JapaneseCandleService::initializeJapaneseCandle(long long timestamp) {
    const auto aligned = domain::floorToMinuteMs(timestamp);
    candle.setNewTime(core::Timestamp(aligned));

    if (!db || !db->isTimestampWithinRange(aligned)) {
        candle.setDataReady(false);
        candle.resetClosePriceIndicator();
        return;
    }

    double o = 0.0;
    double h = 0.0;
    double l = 0.0;
    double c = 0.0;
    if (db->tryGetOHLC(aligned, o, h, l, c)) {
        setOHLC(infra::storage::DatabaseEngine::OHLC{ aligned, o, h, l, c });
    }
    else {
        candle.setDataReady(false);
        candle.resetClosePriceIndicator();
        if (g_missingDataLimiter.shouldReport("OHLC", aligned)) {
            LOG_WARN(logging::LogCategory::DATA,
                     "JapaneseCandleService missing OHLC for timestamp %lld",
                     static_cast<long long>(aligned));
        }
    }
}

void JapaneseCandleService::setOHLC(const infra::storage::DatabaseEngine::OHLC& data) {
    candle.setNewTime(core::Timestamp(data.openTimeMs));
    candle.setOpenPrice(data.open);
    candle.setClosePrice(data.close);
    candle.setMaxPrice(data.high);
    candle.setMinPrice(data.low);
    candle.setDataReady(false);
    actualize();
}

void JapaneseCandleService::setPriceData(const infra::storage::PriceData& data) {
    const auto aligned = domain::floorToMinuteMs(data.openTime);
    setOHLC(infra::storage::DatabaseEngine::OHLC{ aligned, data.openPrice, data.highPrice, data.lowPrice, data.closePrice });
}

void JapaneseCandleService::actualize() {
    const auto openPrice = candle.getOpenPrice();
    const auto closePrice = candle.getClosePrice();
    const auto maxPrice = candle.getMaxPrice();
    const auto minPrice = candle.getMinPrice();

    if (!gridTime || !gridValues || !(openPrice && closePrice && maxPrice && minPrice)) {
        candle.setDataReady(false);
        if (gridValues) {
            configureClosePriceText(*gridValues);
        }
        else {
            candle.resetClosePriceIndicator();
        }
        return;
    }

    const auto toCents = [](double value) {
        return static_cast<float>(std::round(value * 100.0));
    };

    const float openValueCents = toCents(*openPrice);
    const float closeValueCents = toCents(*closePrice);
    const float maxValueCents = toCents(*maxPrice);
    const float minValueCents = toCents(*minPrice);

    const auto timeMinutes = domain::millisToMinutes(candle.getTime().getTimestamp());

    candle.setBodyPosition(sf::Vector2f(
        gridTime->getValuePosX(timeMinutes),
        gridValues->getValuePosY(std::max(closeValueCents, openValueCents))));

    float bodyHeight = gridValues->getValuePosY(std::min(openValueCents, closeValueCents)) -
        gridValues->getValuePosY(std::max(closeValueCents, openValueCents));
    bodyHeight = std::max(bodyHeight, 2.0f);
    candle.setBodySize(sf::Vector2f(35.0f, bodyHeight));
    candle.setBodyFillColor(*openPrice > *closePrice ? sf::Color(231, 76, 60) : sf::Color(46, 204, 113));

    candle.setWickPosition(sf::Vector2f(
        gridTime->getValuePosX(timeMinutes),
        gridValues->getValuePosY(std::max(maxValueCents, minValueCents))));
    candle.setWickSize(sf::Vector2f(2.0f,
        gridValues->getValuePosY(std::min(minValueCents, maxValueCents)) -
        gridValues->getValuePosY(std::max(maxValueCents, minValueCents))));
    candle.setWickFillColor(candle.getBody().getFillColor());

    candle.setDataReady(true);
    configureClosePriceText(*gridValues);
    centerBodyAndWick();
}

void JapaneseCandleService::UIEventHandler(sf::Event e) {
    // Comprobar si el evento es relevante (movimiento del rat�n)
    if (!candle.isReady()) {
        return;
    }
    if (e.type == sf::Event::MouseMoved) {
        sf::Vector2f mousePosition(static_cast<float>(e.mouseMove.x), static_cast<float>(e.mouseMove.y));

        // Obtener las dimensiones y posici�n de la vela
    const auto& body = candle.getBody();
    sf::FloatRect candleBounds = body.getGlobalBounds();

        // Verificar si el mouse est� sobre la vela
        if (candleBounds.contains(mousePosition)) {
            // Llama a un m�todo de la vela para dibujar el cuadro con la informaci�n
            // showToolTip = true;
            // candleToolTipService.updateTooltipPosition();
            // candleToolTipService.updateTooltipText();
        }
        else {
            // Opcional: Ocultar el cuadro si el mouse no est� sobre la vela
            showToolTip = false;
        }
    }
}

void JapaneseCandleService::centerBodyAndWick() {
    if (!candle.isReady()) {
        return;
    }
    const auto& body = candle.getBody();
    sf::Vector2f bodySize = body.getSize();
    candle.setBodyOrigin(sf::Vector2f(bodySize.x / 2.0f, body.getOrigin().y));

    const auto& wick = candle.getWick();
    sf::Vector2f wickSize = wick.getSize();
    candle.setWickOrigin(sf::Vector2f(wickSize.x / 2.0f, wick.getOrigin().y));
}

void JapaneseCandleService::configureClosePriceText(ui::GridValues& gridValues) {
    const auto closePrice = candle.getClosePrice();
    if (!closePrice || !candle.hasFont()) {
        candle.resetClosePriceIndicator();
        return;
    }

    std::array<char, 32> buffer{};
    const double price = std::round((*closePrice) * 100.0) / 100.0;
    std::snprintf(buffer.data(), buffer.size(), "%.2f", price);

    sf::Text& closePriceText = candle.accessClosePriceText();
    closePriceText.setString(buffer.data());
    closePriceText.setCharacterSize(18);
    closePriceText.setFillColor(sf::Color(200, 205, 220));
    sf::FloatRect textBounds = closePriceText.getLocalBounds();
    closePriceText.setOrigin(textBounds.width / 2, textBounds.height);
    const float closeValueCents = static_cast<float>(std::round((*closePrice) * 100.0));
    closePriceText.setPosition(1850, gridValues.getValuePosY(closeValueCents));
    candle.setClosePriceHorizontalLinePosition(closePriceText.getPosition().y);
    candle.setClosePriceIndicatorReady(true);
}

JapaneseCandle& JapaneseCandleService::operator+=(int n) {
    initializeJapaneseCandle((candle.getTime() += n).getTimestamp());
    return candle;
}

JapaneseCandle& JapaneseCandleService::operator-=(int n) {
    initializeJapaneseCandle((candle.getTime() -= n).getTimestamp());
    return candle;
}

// void JapaneseCandleService::draw() {
//     candle.drawCandle(*w);
//     if(subscribedToFullData) candle.drawClosePriceIndicator(*w);
//     if (showToolTip) candleToolTipService.draw(*w);
// }


void JapaneseCandleService::draw() {
    // renderManager->addRenderCommand(1, [this](sf::RenderTarget& win) {
    //     candle.drawCandle(win);
    //     if (subscribedToFullData)
    //         candle.drawClosePriceIndicator(win);
    //     // if (showToolTip)
    //     //     candleToolTipService.draw(win);
    // });
    
}








//OBSERVER

// M�todos de las interfaces
void JapaneseCandleService::onCacheUpdated(const infra::storage::PriceData& priceData) {//Una vez por minuto
    setPriceData(priceData);
}

void JapaneseCandleService::onFullDataUpdated(const infra::storage::PriceData& priceData) {//ante cada cambio
    setPriceData(priceData);
}

// M�todos de suscripci�n
void JapaneseCandleService::subscribeToCache(int cacheIndex) {
    db->addObserver(this, cacheIndex); // Usa un �ndice apropiado si es necesario
    subscribedToCache = true;
}

void JapaneseCandleService::unsubscribeFromCache() {
    db->removeObserver(this);
    subscribedToCache = false;
}

void JapaneseCandleService::subscribeToFullData() {
    db->addFullDataObserver(this);
    subscribedToFullData = true;
}

void JapaneseCandleService::unsubscribeFromFullData() {
    db->removeFullDataObserver(this);
    subscribedToFullData = false;
}

//GETTERS

std::optional<double> JapaneseCandleService::getMaxPrice() const {
    return candle.getMaxPrice();
}

std::optional<double> JapaneseCandleService::getMinPrice() const {
    return candle.getMinPrice();
}

std::optional<double> JapaneseCandleService::getOpenPrice() const {
    return candle.getOpenPrice();
}

std::optional<double> JapaneseCandleService::getClosePrice() const {
    return candle.getClosePrice();
}

const sf::RectangleShape& JapaneseCandleService::getBody() const {
    return candle.getBody();
}

bool JapaneseCandleService::isReady() const {
    return candle.isReady();
}

long long JapaneseCandleService::getTimestamp() const {
    return candle.getTime().getTimestamp();
}

std::string JapaneseCandleService::getDateTime(){
    return candle.getTime().getString();
}

float JapaneseCandleService::getPosX() {
    if (!candle.isReady()) {
        return 0.0f;
    }
    return candle.getBody().getPosition().x;
}

JapaneseCandleService::~JapaneseCandleService() {
    // Desuscribirse del EventBus para evitar callbacks colgantes.
    if (eventBus) {
        eventBus->unsubscribe(sf::Event::MouseMoved, uiEventHandlerToken);
    }
}

}  // namespace ui
















