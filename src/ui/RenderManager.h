#pragma once

#include <SFML/Graphics/View.hpp>
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <functional>
#include <vector>

#include "core/RenderCommand.h"

namespace ui {

class RenderManager {
public:
    RenderManager();
    ~RenderManager();

    // Agrega un comando de renderizado con un z-index
    void addRenderCommand(int z, std::function<void(sf::RenderTarget&)> func);

    // Redibuja todos los comandos encolados (limpia la ventana y dibuja todo)
    void render(sf::RenderWindow& window);

    // Verifica si hay comandos pendientes (opcional)
    bool hasCommands() const;

    // Reacciona a cambios de tamaño del canvas
    void onResize(sf::Vector2u size);

private:
    std::vector<core::RenderCommand> commands;
    sf::Vector2u canvasSize_;
    sf::View windowView_;
    bool viewDirty_{true};
};

} // namespace ui

