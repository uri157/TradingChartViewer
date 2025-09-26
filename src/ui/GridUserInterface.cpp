#include "ui/GridUserInterface.h"

#include "bootstrap/DIContainer.h"
#include "logging/Log.h"

#include <iostream>

namespace ui {

GridUserInterface::GridUserInterface() {
    resourceProvider = bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider");
    if (resourceProvider) {
        auto fontResource = resourceProvider->getFontResource("ui");
        font = fontResource.font;
        hasFont_ = fontResource.ready;
        if (!hasFont_ && !warningLogged_) {
            LOG_WARN(logging::LogCategory::UI,
                     "GridUserInterface text disabled: font could not be loaded.");
            warningLogged_ = true;
        }
        if (hasFont_ && font) {
            gridName.setFont(*font);
            interval1m.setFont(*font);
        }
    }

    gridName.setString("Bitcoin/USDT BINANCE");
    gridName.setCharacterSize(18);
    gridName.setFillColor(sf::Color(200, 205, 220));
    gridName.setPosition(200.f, 50.f);
    sf::FloatRect nameBounds = gridName.getLocalBounds();
    gridName.setOrigin(nameBounds.width / 2, nameBounds.height / 2);

    interval1m.setString("1m");
    interval1m.setCharacterSize(18);
    interval1m.setFillColor(sf::Color(200, 205, 220));
    interval1m.setPosition(200.f, 25.f);
    sf::FloatRect intervalBounds = interval1m.getLocalBounds();
    interval1m.setOrigin(intervalBounds.width / 2, intervalBounds.height / 2);
    intervals.push_back(&interval1m);
    ready_ = hasFont_;
}

void GridUserInterface::draw(sf::RenderWindow& w) {
    if (!isReady()) {
        return;
    }
    w.draw(gridName, sf::RenderStates::Default);
    for (sf::Text* interval : intervals) {
        if (interval) {
            w.draw(*interval, sf::RenderStates::Default);
        }
    }
}

bool GridUserInterface::isReady() const {
    return ready_ && hasFont_;
}

} // namespace ui

