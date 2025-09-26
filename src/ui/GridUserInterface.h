#pragma once

#include "ui/ResourceProvider.h"
#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>

namespace ui {

class GridUserInterface {
        std::shared_ptr<sf::Font> font;
        sf::Text gridName; /*(BTC/USDT), (RSI), (EMA X), etc*/
        sf::Text interval1m;
        std::vector<sf::Text*> intervals;
        ResourceProvider* resourceProvider;
        bool hasFont_{false};
        bool ready_{false};
        bool warningLogged_{false};
public:
        GridUserInterface();
        void draw(sf::RenderWindow& w);
        bool isReady() const;
};

} // namespace ui
