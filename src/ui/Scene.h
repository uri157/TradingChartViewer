#pragma once

#include <SFML/Window/Event.hpp>

namespace sf {
class RenderWindow;
}

namespace ui {

class Cursor;

class Scene {
protected:
    sf::RenderWindow* w;
    Cursor* crsor;

public:
    Scene();
    virtual ~Scene();

    virtual void actualize(sf::Event& event) = 0;
    virtual void draw() = 0;
};

}  // namespace ui

