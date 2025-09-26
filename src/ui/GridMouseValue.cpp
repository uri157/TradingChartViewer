#include "ui/GridMouseValue.h"
#include "ui/GridValues.h"

#include "ui/Cursor.h"

#include "bootstrap/DIContainer.h"

namespace ui {

GridMouseValue::GridMouseValue(GridValues* gridValues)
    : GridValue(0),
      cursor(bootstrap::DIContainer::resolve<ui::Cursor>("Cursor")),
      eventBus(bootstrap::DIContainer::resolve<core::EventBus>("EventBus")),
      gridValues(gridValues),
      w(bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow")) {
    uiEventHandlerToken = eventBus->subscribe(sf::Event::MouseMoved, [this](const sf::Event&) {
        this->actualize();
    });
}

void GridMouseValue::actualize() {
    setPositionY(cursor->getCursorPosY());
    *this = gridValues->getValueFromPositionOnY(this->getPositionY());
    draw();
}

GridMouseValue::~GridMouseValue() {
    eventBus->unsubscribe(sf::Event::MouseMoved, uiEventHandlerToken);
}

} // namespace ui

