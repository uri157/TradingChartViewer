#pragma once

#include "bootstrap/DIContainer.h"
#include "ui/ResourceProvider.h"
#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>

namespace ui {

class GridValue {
        long double value=0;
        sf::Text valueInText;
        std::shared_ptr<sf::Font> font;
        std::vector<sf::Vertex> horizontalLine;
        sf::RenderWindow* w;
        ResourceProvider* resourceProvider = nullptr;
        bool hasFont_{false};
        bool warningLogged_{false};
public:
        GridValue() = default;
        GridValue(int val);
        void draw();
        float getValue() const;
	// Sobrecarga de operadores
	GridValue& operator=(const GridValue& other);
	GridValue& operator=(float newValue);
	GridValue& operator+=(long double newValue);
	GridValue& operator-=(long double newValue);
        // M�todos para establecer la posici�n
        void setPositionX(float x);
        void setPositionY(float y);
        float getPositionY();
        bool isReady() const;
};

} // namespace ui
