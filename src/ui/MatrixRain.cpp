#include "ui/MatrixRain.h"

#include <algorithm>
#include <chrono>

#include "bootstrap/DIContainer.h"
#include "logging/Log.h"

namespace ui {

MatrixRain::MatrixRain(int windowWidth) : gen(rd()) {
    symbols = {"0", "1"};

    charWidth = 30;
    columns = std::max(1, windowWidth / charWidth);

    greenRain.resize(columns);
    greenRainString.resize(columns);
    lastUpdate.resize(columns);
    rainLengths.resize(columns);
    updateSpeeds.resize(columns);

    resourceProvider = bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider");
    if (resourceProvider) {
        auto fontResource = resourceProvider->getFontResource("ui");
        mainFont = fontResource.font;
        hasFont_ = fontResource.ready;
        if (!hasFont_ && !warningLogged_) {
            LOG_WARN(logging::LogCategory::UI, "MatrixRain text disabled: font could not be loaded.");
            warningLogged_ = true;
        }
    }

    for (int i = 0; i < columns; i++) {
        if (hasFont_ && mainFont) {
            greenRain[i].setFont(*mainFont);
        }

        sf::Color transparentGreen(0, 255, 0, 30);
        greenRain[i].setFillColor(transparentGreen);

        int charSize = 10 + gen() % 20;
        greenRain[i].setCharacterSize(charSize);
        greenRain[i].setPosition(static_cast<float>(charWidth * i), 0.f);
        greenRain[i].setString("0\n1\n0");

        rainLengths[i] = 0;
        updateSpeeds[i] = 120 + (40 - charSize);
        lastUpdate[i] = std::chrono::steady_clock::now();
    }
    ready_ = hasFont_;
}

void MatrixRain::actualize() {
    if (!isReady()) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < columns; i++) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate[i]).count() >= updateSpeeds[i]) {
            greenRainString[i] = nextFrame(std::move(greenRainString[i]), i);
            greenRain[i].setString(greenRainString[i]);
            lastUpdate[i] = now;
        }
    }
}

std::string MatrixRain::nextFrame(std::string input, int i) {
    bool addSpace = rainLengths[i] <= 0;
    if (!addSpace) {
        input.insert(0, symbols[gen() % symbols.size()] + "\n");
    }
    else {
        input.insert(0, "\n");
    }

    constexpr std::size_t maxLines = 20;
    std::size_t newlineCount = std::count(input.begin(), input.end(), '\n');
    if (newlineCount > maxLines) {
        std::size_t pos = 0;
        for (std::size_t count = 0; count < newlineCount - maxLines; ++count) {
            pos = input.find_last_of('\n');
            if (pos == std::string::npos) {
                break;
            }
            input.erase(pos);
        }
    }

    rainLengths[i] -= 1;

    if (addSpace && gen() % 100 < 5) {
        rainLengths[i] = 15;
    }

    constexpr std::size_t maxSize = 50;
    if (input.size() > maxSize) {
        input.resize(maxSize);
    }

    return input;
}

void MatrixRain::draw(sf::RenderWindow& w) {
    if (!isReady()) {
        return;
    }
    for (int i = 0; i < columns; i++) {
        w.draw(greenRain[i], sf::RenderStates::Default);
    }
}

double MatrixRain::calculateMemoryUsage(bool detailed) const {
    size_t totalSize = 0;

    size_t greenRainSize = sizeof(greenRain) + (sizeof(sf::Text) * greenRain.capacity());
    size_t greenRainStringSize = sizeof(greenRainString) + (sizeof(std::string) * greenRainString.capacity());
    size_t lastUpdateSize = sizeof(lastUpdate) + (sizeof(std::chrono::steady_clock::time_point) * lastUpdate.capacity());
    size_t rainLengthsSize = sizeof(rainLengths) + (sizeof(int) * rainLengths.capacity());
    size_t updateSpeedsSize = sizeof(updateSpeeds) + (sizeof(int) * updateSpeeds.capacity());
    size_t symbolsSize = sizeof(symbols) + (sizeof(std::string) * symbols.capacity());

    for (const auto& str : greenRainString) {
        greenRainStringSize += str.capacity();
    }

    for (const auto& symbol : symbols) {
        symbolsSize += symbol.capacity();
    }

    totalSize += greenRainSize;
    totalSize += greenRainStringSize;
    totalSize += lastUpdateSize;
    totalSize += rainLengthsSize;
    totalSize += updateSpeedsSize;
    totalSize += symbolsSize;

    totalSize += sizeof(mainFont);
    totalSize += sizeof(columns);
    totalSize += sizeof(charWidth);
    totalSize += sizeof(rd);
    totalSize += sizeof(gen);

    if (detailed) {
        LOG_DEBUG(logging::LogCategory::RENDER,
                  "Vector greenRain: %.3f KB, size=%zu",
                  static_cast<double>(greenRainSize) / 1024.0,
                  static_cast<std::size_t>(greenRain.size()));
        LOG_DEBUG(logging::LogCategory::RENDER,
                  "Vector greenRainString: %.3f KB, size=%zu",
                  static_cast<double>(greenRainStringSize) / 1024.0,
                  static_cast<std::size_t>(greenRainString.size()));
        LOG_DEBUG(logging::LogCategory::RENDER,
                  "Vector lastUpdate: %.3f KB, size=%zu",
                  static_cast<double>(lastUpdateSize) / 1024.0,
                  static_cast<std::size_t>(lastUpdate.size()));
        LOG_DEBUG(logging::LogCategory::RENDER,
                  "Vector rainLengths: %.3f KB, size=%zu",
                  static_cast<double>(rainLengthsSize) / 1024.0,
                  static_cast<std::size_t>(rainLengths.size()));
        LOG_DEBUG(logging::LogCategory::RENDER,
                  "Vector updateSpeeds: %.3f KB, size=%zu",
                  static_cast<double>(updateSpeedsSize) / 1024.0,
                  static_cast<std::size_t>(updateSpeeds.size()));
        LOG_DEBUG(logging::LogCategory::RENDER,
                  "Vector symbols: %.3f KB, size=%zu",
                  static_cast<double>(symbolsSize) / 1024.0,
                  static_cast<std::size_t>(symbols.size()));
    }

    return static_cast<double>(totalSize) / (1024 * 1024);
}

bool MatrixRain::isReady() const {
    return ready_ && hasFont_;
}

}  // namespace ui
