//JapaneseCandle.cpp
#include "ui/JapaneseCandle.h"

#include <algorithm>
#include <cmath>

#include "bootstrap/DIContainer.h"
#include "logging/Log.h"

namespace ui {

JapaneseCandle::JapaneseCandle(core::Timestamp time)
    : time(time) {
    resourceProvider = bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider");
    if (resourceProvider) {
        auto fontResource = resourceProvider->getFontResource("ui");
        font = fontResource.font;
        hasFont_ = fontResource.ready;
        if (!hasFont_ && !fontWarningLogged_) {
            LOG_WARN(logging::LogCategory::UI, "JapaneseCandle text disabled: font could not be loaded.");
            fontWarningLogged_ = true;
        }
        if (hasFont_ && font) {
            closePriceText.setFont(*font);
        }
    }
    closePriceHorizontalLine.push_back(sf::Vertex(sf::Vector2f(0.f, 0.f), sf::Color(46, 52, 64)));
    closePriceHorizontalLine.push_back(sf::Vertex(sf::Vector2f(1800.f, 0.f), sf::Color(46, 52, 64)));
}

void JapaneseCandle::setClosePriceHorizontalLinePosition(float newPosition) {
    if (closePriceHorizontalLine.size() >= 2) {
        closePriceHorizontalLine[0].position.y = closePriceHorizontalLine[1].position.y = newPosition;
        closePriceIndicatorReady_ = hasFont_;
    }
}

void JapaneseCandle::setNewTime(core::Timestamp newTime) {
    time = newTime;
   /* initializeValues();*/
}



void JapaneseCandle::drawCandle(sf::RenderTarget& window) {
    if (!isReady()) {
        return;
    }
    window.draw(wick, sf::RenderStates::Default);
    window.draw(body, sf::RenderStates::Default);
}

void JapaneseCandle::drawClosePriceIndicator(sf::RenderTarget& window) {
    if (!isClosePriceIndicatorReady()) {
        return;
    }
    window.draw(closePriceText, sf::RenderStates::Default);
    if (closePriceHorizontalLine.size() >= 2) {
        window.draw(&closePriceHorizontalLine[0], closePriceHorizontalLine.size(), sf::Lines);
    }
}


//SETTERS

void JapaneseCandle::setClosePrice(std::optional<double> val) {
    if (val) {
        closePrice = static_cast<float>(*val * 100.0);
    }
    else {
        closePrice.reset();
    }
}

void JapaneseCandle::setOpenPrice(std::optional<double> val) {
    if (val) {
        openPrice = static_cast<float>(*val * 100.0);
    }
    else {
        openPrice.reset();
    }
}

void JapaneseCandle::setMaxPrice(std::optional<double> val) {
    if (val) {
        maxPrice = static_cast<float>(*val * 100.0);
    }
    else {
        maxPrice.reset();
    }
}

void JapaneseCandle::setMinPrice(std::optional<double> val) {
    if (val) {
        minPrice = static_cast<float>(*val * 100.0);
    }
    else {
        minPrice.reset();
    }
}

void JapaneseCandle::setBodySize(sf::Vector2f newSize) {
    body.setSize(newSize);
}
void JapaneseCandle::setBodyFillColor(sf::Color newColor) {
    body.setFillColor(newColor);
}

void JapaneseCandle::setBodyPosition(sf::Vector2f newPosition) {
    body.setPosition(newPosition);
}

void JapaneseCandle::setWickSize(sf::Vector2f newSize) {
    wick.setSize(newSize);
}
void JapaneseCandle::setWickFillColor(sf::Color newColor) {
    wick.setFillColor(newColor);
}

void JapaneseCandle::setWickPosition(sf::Vector2f newPosition) {
    wick.setPosition(newPosition);
}

sf::Text& JapaneseCandle::accessClosePriceText() {
    return closePriceText;
}

void JapaneseCandle::setBodyOrigin(sf::Vector2f newOrigin)
{
    body.setOrigin(newOrigin);
}

void JapaneseCandle::setWickOrigin(sf::Vector2f newOrigin)
{
    wick.setOrigin(newOrigin);
}



//GETTERS
core::Timestamp JapaneseCandle::getTime() const {
    return time;
}

std::optional<double> JapaneseCandle::getClosePrice() const{
    if (closePrice) {
        return static_cast<double>(*closePrice) / 100.0;
    }
    return std::nullopt;
}

std::optional<double> JapaneseCandle::getOpenPrice() const{
    if (openPrice) {
        return static_cast<double>(*openPrice) / 100.0;
    }
    return std::nullopt;
}

std::optional<double> JapaneseCandle::getMaxPrice() const{
    if (maxPrice) {
        return static_cast<double>(*maxPrice) / 100.0;
    }
    return std::nullopt;
}

std::optional<double> JapaneseCandle::getMinPrice() const{
    if (minPrice) {
        return static_cast<double>(*minPrice) / 100.0;
    }
    return std::nullopt;
}

const sf::RectangleShape& JapaneseCandle::getBody() const {
    return body;
}

const sf::RectangleShape& JapaneseCandle::getWick() const {
    return wick;
}

bool JapaneseCandle::isReady() const {
    return geometryReady_;
}

bool JapaneseCandle::isClosePriceIndicatorReady() const {
    return closePriceIndicatorReady_ && hasFont_;
}

bool JapaneseCandle::hasFont() const {
    return hasFont_;
}

void JapaneseCandle::setDataReady(bool ready) {
    geometryReady_ = ready;
    if (!ready) {
        resetClosePriceIndicator();
    }
}

void JapaneseCandle::setClosePriceIndicatorReady(bool ready) {
    closePriceIndicatorReady_ = ready && hasFont_ && closePriceHorizontalLine.size() >= 2;
}

void JapaneseCandle::resetClosePriceIndicator() {
    closePriceIndicatorReady_ = false;
}

}  // namespace ui
