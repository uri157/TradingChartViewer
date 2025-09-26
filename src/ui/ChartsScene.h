#pragma once

#include "ui/Grid.h"
#include "ui/Scene.h"

namespace ui {

class ChartsScene : public Scene {
        Grid* tradingGrid;
public:
        ChartsScene();
        void actualize(sf::Event& event) override;
        void draw() override;
};

}  // namespace ui

