#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/System/Clock.hpp>
#include <chrono>

#include "ui/MatrixRain.h"
#include "ui/GridUserInterface.h"

// Forward declarations para evitar headers pesados en el .h
namespace infra { namespace storage { class DatabaseEngine; } }

namespace ui {

class GridTime;
class GridValues;
class GridJapaneseCandle;
class Cursor;

class Grid {
    int                            scopeId;
    MatrixRain                     rain;
    GridUserInterface              userInterface;
    infra::storage::DatabaseEngine* db;
    GridTime*                      gridTime;
    double                         memoryUsage = 0;
    GridValues*                    gridValues;
    GridJapaneseCandle*            japaneseCandles;
    Cursor*                        crsor;
    sf::Vector2f                   lastMousePos;
    bool                           isDragging = false, spectatorModeOn = true;
    sf::Clock                      zoomClock;
    std::chrono::steady_clock::time_point lastUpdate;

public:
    Grid(int scopeId);
    void draw(sf::RenderWindow& w);
    void actualize(sf::Event& event);
    /*double memoryStatus();*/
    ~Grid();
};

} // namespace ui
