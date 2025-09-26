#pragma once

#include <SFML/Graphics.hpp>

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "ui/ResourceProvider.h"

namespace ui {

class MatrixRain {
    std::shared_ptr<sf::Font> mainFont;
    std::vector<sf::Text> greenRain;
    std::vector<std::string> greenRainString;
    std::vector<std::chrono::steady_clock::time_point> lastUpdate;
    std::vector<int> rainLengths;
    std::vector<int> updateSpeeds;
    std::vector<std::string> symbols;
    int columns;
    int charWidth;
    double memoryUsage = 0;
    std::random_device rd;  // Generador de semilla
    std::mt19937 gen;
    ResourceProvider* resourceProvider = nullptr;
    bool hasFont_{false};
    bool ready_{false};
    bool warningLogged_{false};

public:
    MatrixRain(int windowWidth);
    void actualize();
    std::string nextFrame(std::string input, int i);
    void draw(sf::RenderWindow& w);
    double calculateMemoryUsage(bool detailed=false) const;
    bool isReady() const;
};

}  // namespace ui
