#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include "bootstrap/DIContainer.h"
#include "ui/ResourceProvider.h"
#include "core/EventBus.h"
#include "ui/RenderManager.h"
#include <memory>

namespace ui {

class Cursor {
        std::shared_ptr<sf::Texture> cursorTexture;
        sf::Sprite cursorSprite;
        sf::RenderWindow* w;
        sf::VertexArray horizontalLine;/*,
                                        verticalLine;*/
        core::EventBus* eventBus;
        RenderManager* renderManager;
        ResourceProvider* resourceProvider;
        bool textureReady_{false};
        bool lineReady_{false};
        bool warningLogged_{false};
public:
        Cursor();
        void actualize();
        float getCursorPosX();
        float getCursorPosY();
        void onMouseMove();
        void draw();
        bool isTextureReady() const;
        bool isLineReady() const;
        bool isReady() const;
};

} // namespace ui
