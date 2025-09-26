#include "ui/RenderManager.h"

#include "logging/Log.h"

namespace ui {

RenderManager::RenderManager()
    : canvasSize_{0, 0},
      windowView_(sf::FloatRect(0.f, 0.f, 1.f, 1.f)),
      viewDirty_{true} {}

RenderManager::~RenderManager() = default;

void RenderManager::addRenderCommand(int z, std::function<void(sf::RenderTarget&)> func) {
    commands.emplace_back(z, std::move(func));
}

void RenderManager::render(sf::RenderWindow& window) {
    if (viewDirty_) {
        const sf::Vector2u size = (canvasSize_.x == 0 || canvasSize_.y == 0)
            ? window.getSize()
            : canvasSize_;
        if (size.x > 0 && size.y > 0) {
            windowView_.reset(sf::FloatRect(0.f, 0.f, static_cast<float>(size.x), static_cast<float>(size.y)));
            window.setView(windowView_);
        }
        viewDirty_ = false;
    }

    window.clear(sf::Color(20, 20, 20));

    std::sort(commands.begin(), commands.end(), [](const core::RenderCommand& a, const core::RenderCommand& b) {
        return a.zIndex < b.zIndex;
    });

    for (auto& cmd : commands) {
        cmd.drawFunc(window);
    }

    commands.clear();
}

bool RenderManager::hasCommands() const {
    return !commands.empty();
}

void RenderManager::onResize(sf::Vector2u size) {
    if (size.x == 0 || size.y == 0) {
        return;
    }

    if (size == canvasSize_) {
        return;
    }

    canvasSize_ = size;
    viewDirty_ = true;
    LOG_INFO(logging::LogCategory::UI,
             "RenderManager resize width=%u height=%u",
             static_cast<unsigned>(size.x),
             static_cast<unsigned>(size.y));
}

} // namespace ui

