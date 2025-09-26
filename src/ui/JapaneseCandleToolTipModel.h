#pragma once

#include <SFML/Graphics.hpp>

#include <memory>
#include <string>

#include "ui/ResourceProvider.h"

namespace ui {

class JapaneseCandleToolTipModel {
public:
    JapaneseCandleToolTipModel();

    void setText(const std::string& newText);
    void setPosition(const sf::Vector2f& newPosition);
    void setCharacterSize(unsigned int size);

    const sf::Text& getText() const;

    void setTextFillColor(sf::Color newColor);

    void draw(sf::RenderTarget& w);

    bool isReady() const;

private:
    sf::Text tooltipText;
    std::shared_ptr<sf::Font> font;
    ResourceProvider* resourceProvider = nullptr;
    bool hasFont_{false};
    bool ready_{false};
    bool warningLogged_{false};
};

}  // namespace ui
