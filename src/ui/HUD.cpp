#include "ui/HUD.h"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <string>

#include "ui/ResourceProvider.h"
#include "logging/Log.h"

namespace ui {

void HUD::drawStateOverlay(sf::RenderTarget& target,
                           const core::RenderSnapshot& snapshot,
                           ResourceProvider& resourceProvider) {
    const sf::Vector2u windowSize = target.getSize();
    if (windowSize.x == 0 || windowSize.y == 0) {
        return;
    }

    const float pad = 12.f;
    const float bannerH = 44.f;
    const sf::Vector2f pos(pad, pad);
    const sf::Vector2f size(static_cast<float>(windowSize.x) - 2.f * pad, bannerH);

    sf::RectangleShape rect(size);
    rect.setPosition(pos);

    sf::Color bg(0, 0, 0, 160);
    switch (snapshot.state) {
    case core::UiState::NoData:
        bg = sf::Color(128, 128, 128, 180);
        break;
    case core::UiState::Loading:
        bg = sf::Color(70, 130, 180, 180);
        break;
    case core::UiState::Live:
        bg = sf::Color(46, 139, 87, 180);
        break;
    case core::UiState::Desync:
        bg = sf::Color(205, 92, 92, 180);
        break;
    }
    rect.setFillColor(bg);
    target.draw(rect);

    auto font = resourceProvider.getFont("ui");
    if (!font) {
        if (!fontWarningLogged_) {
            LOG_WARN(logging::LogCategory::UI, "HUD overlay: font not available.");
            fontWarningLogged_ = true;
        }
        return;
    }

    std::string message = snapshot.stateMessage.empty() ? std::string(" ") : snapshot.stateMessage;
    sf::Text text;
    text.setFont(*font);
    text.setCharacterSize(18);
    text.setString(message);
    text.setFillColor(sf::Color::White);
    text.setPosition(pos.x + 12.f, pos.y + 10.f);
    target.draw(text);
}

} // namespace ui

