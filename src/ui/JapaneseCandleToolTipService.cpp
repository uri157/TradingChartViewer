#include "ui/JapaneseCandleToolTipService.h"

#include <optional>
#include <sstream>

#include "ui/JapaneseCandleService.h"

namespace ui {

JapaneseCandleToolTipService::JapaneseCandleToolTipService(JapaneseCandleService& candleService)
    : candleService(candleService) {
}

void JapaneseCandleToolTipService::updateTooltipPosition() {
    const auto& body = candleService.getBody();
    sf::Vector2f bodyPosition = body.getPosition();
    sf::Vector2f bodySize = body.getSize();
    sf::Vector2f bodyOrigin = body.getOrigin();

    sf::Vector2f adjustedPosition = bodyPosition - bodyOrigin;
    sf::Vector2f tooltipPosition(adjustedPosition.x + bodySize.x, adjustedPosition.y + bodySize.y);

    model.setPosition(tooltipPosition);
}

void JapaneseCandleToolTipService::updateTooltipText() {
    std::ostringstream newText;
    newText << candleService.getDateTime() << std::endl;
    auto formatValue = [](const std::optional<double>& value) {
        if (!value) {
            return std::string("N/A");
        }
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(2);
        oss << *value;
        return oss.str();
    };

    newText << "Open Price: " << formatValue(candleService.getOpenPrice()) << std::endl;
    newText << "Max Price: " << formatValue(candleService.getMaxPrice()) << std::endl;
    newText << "Min Price: " << formatValue(candleService.getMinPrice()) << std::endl;
    newText << "Close Price: " << formatValue(candleService.getClosePrice()) << std::endl;
    model.setText(newText.str());
    model.setCharacterSize(24);
    model.setTextFillColor(sf::Color(200, 205, 220));
}

void JapaneseCandleToolTipService::draw(sf::RenderTarget& w) {
    if (!model.isReady()) {
        return;
    }
    model.draw(w);
}

}  // namespace ui
