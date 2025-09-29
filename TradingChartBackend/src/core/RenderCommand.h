#pragma once

#if 0
// TODO: legacy-ui

#include <functional>
#include <utility>

namespace core {

struct RenderCommand {
    int zIndex;  // Orden de dibujo: menor se dibuja primero
    std::function<void(sf::RenderTarget&)> drawFunc; // Función que realiza el dibujo

    RenderCommand(int z, std::function<void(sf::RenderTarget&)> func)
        : zIndex(z), drawFunc(std::move(func)) {}

    RenderCommand(RenderCommand&&) noexcept = default;
    RenderCommand& operator=(RenderCommand&&) noexcept = default;
    RenderCommand(const RenderCommand&) = default;
    RenderCommand& operator=(const RenderCommand&) = default;
};

}  // namespace core
#endif  // legacy-ui

#include <functional>

namespace core {

struct RenderCommand {
    int zIndex{0};
    std::function<void()> drawFunc{};
};

}  // namespace core
