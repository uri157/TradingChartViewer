#include "ui/ChartsScene.h"

#include "bootstrap/DIContainer.h"
#include "ui/Grid.h"

namespace ui {

ChartsScene::ChartsScene()
    : tradingGrid(bootstrap::DIContainer::resolve<ui::Grid>("Grid")) {}

void ChartsScene::actualize(sf::Event& event) {
    (void)event;
    // tradingGrid->actualize(event);
}

void ChartsScene::draw() {
    tradingGrid->draw(*w);
}

}  // namespace ui
