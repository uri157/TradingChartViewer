#include "ui/Scene.h"

#include "bootstrap/DIContainer.h"
#include "ui/Cursor.h"

#include <SFML/Graphics/RenderWindow.hpp>

namespace ui {

Scene::Scene() {
    w = bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow");
    crsor = bootstrap::DIContainer::resolve<ui::Cursor>("Cursor");
}

Scene::~Scene() = default;

}  // namespace ui

