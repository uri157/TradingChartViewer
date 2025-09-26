#include "ui/GridValues.h"

#include "bootstrap/DIContainer.h"
#include "infra/storage/DatabaseEngine.h"
#include "ui/Cursor.h"
#include "ui/GridMouseValue.h"
#include "ui/GridValue.h"
#include "core/EventBus.h"
#include "ui/ResourceProvider.h"

#include <SFML/Graphics/RenderWindow.hpp>
#include <cmath>
#include <iostream>
#include <string>

namespace ui {

GridValues::GridValues(int scopeId)
    : scopeId(scopeId),
      cursor(bootstrap::DIContainer::resolve<ui::Cursor>("Cursor")),
      eventBus(bootstrap::DIContainer::resolve<core::EventBus>("EventBus")),
      db(bootstrap::DIContainer::resolve<infra::storage::DatabaseEngine>("DatabaseEngine")),
      w(bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow")) {
    resourceProvider = bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider");
    gridMouseValue = new GridMouseValue(this);

    gridMouseValue->setPositionX(1850);
    gridMouseValue->setPositionY(0);

    valuesVerticalLine[0] = sf::Vertex(sf::Vector2f(1800.f, 0.f), sf::Color(46, 52, 64));
    valuesVerticalLine[1] = sf::Vertex(sf::Vector2f(1800.f, 1080.f), sf::Color(46, 52, 64));
}

void GridValues::spectatorMode(bool onOf) {
    spectatorModeOn = onOf;
    if (onOf) {
        db->addPriceLimitObserver(this);
    } else {
        db->removePriceLimitObserver(this);
    }
}

void GridValues::onMaxPriceLimitChanged(double newMax) {
    maxVal = newMax * 100;
    maxVal += maxVal * 0.001;
    rangeInCents = maxVal - minVal;
    int posiblevaluesinrange = rangeInCents + 1;
    centsperpixel = posiblevaluesinrange / 960.0;
    redefineValuesVector();
}

void GridValues::onMinPriceLimitChanged(double newMin) {
    minVal = newMin * 100;
    minVal -= minVal * 0.001;
    minRealValue = minVal;
    rangeInCents = maxVal - minVal;
    int posiblevaluesinrange = rangeInCents + 1;
    centsperpixel = posiblevaluesinrange / 960.0;
    redefineValuesVector();
}

void GridValues::redefineValuesVector() {
    for (int i = 10; i < 20; i++) {
        if ((rangeInCents / i) % 2 == 0) {
            jumpValue = (rangeInCents / i);
            break;
        }
    }
    if (jumpValue < 1) {
        jumpValue = 1;
    }
    int contador = 0;
    values.clear();
    if (jumpValue != 0) {
        while (minRealValue + contador * jumpValue <= minRealValue + rangeInCents) {
            values.push_back(std::make_shared<GridValue>(minRealValue + contador * jumpValue));
            values[contador]->setPositionX(1850);
            values[contador]->setPositionY(getValuePosY(values[contador]->getValue()));
            contador++;
        }
    }
}

float GridValues::getValuePosY(float value) {
    int valueInCents = value;
    return 960 - ((valueInCents - minRealValue) / centsperpixel);
}

float GridValues::getValueFromPositionOnY(float position) {
    return (minRealValue + (960 - position) * centsperpixel) / 100;
}

void GridValues::displacement(float deltaY) {
    minRealValue += centsperpixel * deltaY;
    for (std::size_t i = 0; i < values.size(); i++) {
        values[i]->setPositionY(getValuePosY(values[i]->getValue()));
        if (values[i]->getPositionY() > 960) {
            *values[i] += rangeInCents;
            values[i]->setPositionY(getValuePosY(values[i]->getValue()));
        } else if (values[i]->getPositionY() < 0) {
            *values[i] -= rangeInCents;
            values[i]->setPositionY(getValuePosY(values[i]->getValue()));
        }
    }
}

double GridValues::calculateMemoryUsage() {
    return 0;
}

void GridValues::zoomOut() {
    if (jumpValue < 1000000) {
        minRealValue -= std::round(0.1 * rangeInCents);
        rangeInCents += std::round(0.2 * rangeInCents);
        int posiblevaluesinrange = rangeInCents + 1;
        centsperpixel = posiblevaluesinrange / 960.0;
        redefineValuesVector();
    }
}

void GridValues::zoomIn() {
    if (jumpValue > 1) {
        minRealValue += std::round(0.1 * rangeInCents);
        rangeInCents -= std::round(0.2 * rangeInCents);
        int posiblevaluesinrange = rangeInCents + 1;
        centsperpixel = posiblevaluesinrange / 960.0;
        redefineValuesVector();
    }
}

void GridValues::draw() {
    // Rendering handled via RenderManager; this function intentionally left blank.
}

GridValues::~GridValues() {
    delete gridMouseValue;
}

} // namespace ui
