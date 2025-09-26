#include "ui/GridValue.h"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>

#include "logging/Log.h"

namespace {

void updateValueText(sf::Text& text, long double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.2Lf", value / 100.0L);
    text.setString(buffer.data());
    sf::FloatRect textBounds = text.getLocalBounds();
    text.setOrigin(textBounds.width / 2, textBounds.height / 2);
}

} // namespace

namespace ui {

GridValue::GridValue(int val)
    : value(val),
      w(bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow")) {
    resourceProvider = bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider");
    if (resourceProvider) {
        auto fontResource = resourceProvider->getFontResource("ui");
        font = fontResource.font;
        hasFont_ = fontResource.ready;
        if (!hasFont_ && !warningLogged_) {
            LOG_WARN(logging::LogCategory::UI, "GridValue text disabled: font could not be loaded.");
            warningLogged_ = true;
        }
        if (hasFont_ && font) {
            valueInText.setFont(*font);
        }
    }
    updateValueText(valueInText, value);
    valueInText.setCharacterSize(18);
    valueInText.setFillColor(sf::Color(200, 205, 220));
    valueInText.setPosition(50.f, 50.f);

    horizontalLine.push_back(sf::Vertex(sf::Vector2f(0.f, 50), sf::Color(46, 52, 64)));
    horizontalLine.push_back(sf::Vertex(sf::Vector2f(1800.f, 50), sf::Color(46, 52, 64)));
}

GridValue& GridValue::operator=(const GridValue& other) {
    if (this == &other) {
        return *this;
    }

    value = other.getValue();
    updateValueText(valueInText, value);
    return *this;
}

GridValue& GridValue::operator+=(long double newValue) {
    value += newValue;
    updateValueText(valueInText, value);
    return *this;
}

GridValue& GridValue::operator-=(long double newValue) {
    value -= newValue;
    updateValueText(valueInText, value);
    return *this;
}

GridValue& GridValue::operator=(float newValue) {
    value = newValue;
    updateValueText(valueInText, value);
    return *this;
}

void GridValue::draw() {
    if (!isReady()) {
        return;
    }
    if (w) {
        w->draw(valueInText);
        w->draw(&horizontalLine[0], horizontalLine.size(), sf::Lines);
    }
}

float GridValue::getValue() const {
    return value;
}

void GridValue::setPositionX(float x) {
    valueInText.setPosition(x, valueInText.getPosition().y);
}

void GridValue::setPositionY(float y) {
    valueInText.setPosition(valueInText.getPosition().x, y);
    if (horizontalLine.size() >= 2) {
        horizontalLine[0].position.y = y;
        horizontalLine[1].position.y = y;
    }
}

float GridValue::getPositionY() {
    return valueInText.getPosition().y;
}

bool GridValue::isReady() const {
    return hasFont_ && horizontalLine.size() >= 2;
}

} // namespace ui

