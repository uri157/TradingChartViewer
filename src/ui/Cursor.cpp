#include "ui/Cursor.h"

#include <SFML/Graphics.hpp>

#include "logging/Log.h"

namespace ui {

Cursor::Cursor()
    : eventBus(bootstrap::DIContainer::resolve<core::EventBus>("EventBus")),
      renderManager(bootstrap::DIContainer::resolve<ui::RenderManager>("RenderManager")),
      w(bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow")),
      resourceProvider(bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider")) {
    if (resourceProvider) {
        auto textureResource = resourceProvider->getTextureResource("cursor");
        cursorTexture = textureResource.texture;
        textureReady_ = textureResource.ready && cursorTexture && cursorTexture->getSize().x > 0
            && cursorTexture->getSize().y > 0;
    }
    if (textureReady_ && cursorTexture) {
        cursorSprite.setTexture(*cursorTexture, true);
    } else {
        sf::CircleShape fallback(6.f);
        fallback.setFillColor(sf::Color::White);
        sf::RenderTexture renderTex;
        if (renderTex.create(16, 16)) {
            renderTex.clear(sf::Color::Transparent);
            renderTex.draw(fallback);
            renderTex.display();
            cursorTexture = std::make_shared<sf::Texture>(renderTex.getTexture());
            cursorSprite.setTexture(*cursorTexture, true);
            textureReady_ = true;
        } else if (!warningLogged_) {
            LOG_WARN(logging::LogCategory::UI,
                     "Cursor fallback texture creation failed. Cursor will not be drawn.");
            warningLogged_ = true;
        }
    }
    sf::FloatRect bounds = cursorSprite.getLocalBounds();
    cursorSprite.setOrigin(bounds.width / 2.0f, bounds.height / 2.0f);
    horizontalLine = sf::VertexArray(sf::Lines, 2);

    for (int i = 0; i < 2; ++i) {
        horizontalLine[i].color = sf::Color::White;
    }
    lineReady_ = horizontalLine.getVertexCount() == 2;

    eventBus->subscribe(sf::Event::MouseMoved, [this](const sf::Event&) { onMouseMove(); });
}

void Cursor::onMouseMove() {
    if (!w) {
        return;
    }
    cursorSprite.setPosition(sf::Vector2f(sf::Mouse::getPosition(*w)));

    sf::Vector2f cursorPosition = cursorSprite.getPosition();

    if (cursorPosition.y < 960) {
        horizontalLine[0].position = sf::Vector2f(0.f, cursorPosition.y);
        horizontalLine[1].position = sf::Vector2f(1800, cursorPosition.y);
    } else {
        horizontalLine[0].position = sf::Vector2f(0.f, 960);
        horizontalLine[1].position = sf::Vector2f(1800, 960);
    }

    draw();
}

float Cursor::getCursorPosX() {
    return cursorSprite.getPosition().x;
}

float Cursor::getCursorPosY() {
    return cursorSprite.getPosition().y;
}

void Cursor::draw() {
    if (!renderManager || !isReady()) {
        return;
    }

    auto spriteCopy = cursorSprite;
    auto lineCopy = horizontalLine;
    auto textureCopy = cursorTexture;
    const bool drawTexture = textureReady_ && textureCopy;
    const bool drawLine = lineReady_ && lineCopy.getVertexCount() == 2;

    renderManager->addRenderCommand(1, [spriteCopy, lineCopy, textureCopy, drawTexture, drawLine](sf::RenderTarget& target) mutable {
        if (drawTexture && textureCopy) {
            sf::Sprite sprite = spriteCopy;
            sprite.setTexture(*textureCopy, true);
            target.draw(sprite, sf::RenderStates::Default);
        }
        if (drawLine) {
            target.draw(lineCopy, sf::RenderStates::Default);
        }
    });
}

bool Cursor::isTextureReady() const {
    return textureReady_;
}

bool Cursor::isLineReady() const {
    return lineReady_;
}

bool Cursor::isReady() const {
    return textureReady_ || lineReady_;
}

} // namespace ui

