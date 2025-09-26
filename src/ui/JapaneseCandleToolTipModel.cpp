#include "ui/JapaneseCandleToolTipModel.h"

#include "bootstrap/DIContainer.h"
#include "logging/Log.h"

namespace ui {

JapaneseCandleToolTipModel::JapaneseCandleToolTipModel() {
    resourceProvider = bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider");
    if (resourceProvider) {
        auto fontResource = resourceProvider->getFontResource("ui");
        font = fontResource.font;
        hasFont_ = fontResource.ready;
        if (!hasFont_ && !warningLogged_) {
            LOG_WARN(logging::LogCategory::UI, "JapaneseCandleToolTipModel text disabled: font could not be loaded.");
            warningLogged_ = true;
        }
        if (hasFont_ && font) {
            tooltipText.setFont(*font);
        }
    }
}

void JapaneseCandleToolTipModel::setText(const std::string& newText) {
    tooltipText.setString(newText);
    ready_ = hasFont_ && !newText.empty();
}

void JapaneseCandleToolTipModel::setPosition(const sf::Vector2f& newPosition) {
    tooltipText.setPosition(newPosition);
}

void JapaneseCandleToolTipModel::setCharacterSize(unsigned int size) {
    tooltipText.setCharacterSize(size);
}

const sf::Text& JapaneseCandleToolTipModel::getText() const {
    return tooltipText;
}

void JapaneseCandleToolTipModel::setTextFillColor(sf::Color newColor) {
    tooltipText.setFillColor(newColor);
}

void JapaneseCandleToolTipModel::draw(sf::RenderTarget& w) {
    if (!isReady()) {
        return;
    }
    w.draw(tooltipText, sf::RenderStates::Default);
}

bool JapaneseCandleToolTipModel::isReady() const {
    return ready_ && hasFont_;
}

}  // namespace ui
