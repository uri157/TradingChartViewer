#pragma once

#include <SFML/Graphics/RenderTarget.hpp>

#include "core/RenderSnapshot.h"

namespace ui {

class ResourceProvider;

class HUD {
public:
    void drawStateOverlay(sf::RenderTarget& target,
                          const core::RenderSnapshot& snapshot,
                          ResourceProvider& resourceProvider);

private:
    bool fontWarningLogged_{false};
};

} // namespace ui
