#pragma once

#include <SFML/Graphics.hpp>

#include "ui/JapaneseCandleToolTipModel.h"

namespace ui {

class JapaneseCandleService;

class JapaneseCandleToolTipService {
public:
    JapaneseCandleToolTipService(JapaneseCandleService& candleService);

    void updateTooltipPosition();

    void updateTooltipText();

    void draw(sf::RenderTarget& w);

private:
    JapaneseCandleService& candleService;
    JapaneseCandleToolTipModel model;

};

}  // namespace ui
