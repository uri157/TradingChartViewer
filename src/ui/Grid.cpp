#include "ui/Grid.h"
#include "bootstrap/DIContainer.h"
#include "infra/storage/DatabaseEngine.h"
#include "ui/Cursor.h"
#include "ui/GridTime.h"
#include "ui/GridValues.h"
#include "ui/GridJapaneseCandle.h"
#include <SFML/Window/Event.hpp>
#include <iostream>

namespace ui {

Grid::Grid(int scopeId) // scopeId(bootstrap::DIContainer::generateScopeId()),
    : scopeId(scopeId),
      gridTime(bootstrap::DIContainer::resolve<ui::GridTime>("GridTime", scopeId)),
      gridValues(bootstrap::DIContainer::resolve<ui::GridValues>("GridValues", scopeId)),
      rain(1920),
      crsor(bootstrap::DIContainer::resolve<ui::Cursor>("Cursor")),
      db(bootstrap::DIContainer::resolve<infra::storage::DatabaseEngine>("DatabaseEngine")),
      japaneseCandles(new ui::GridJapaneseCandle(scopeId)) {
    /*gridValues.spectatorModeActualize(japaneseCandles);*/
    /*japaneseCandles.actualize();*/
    gridValues->spectatorMode(true);
    gridTime->spectatorMode(true);
    // japaneseCandles->spectatorMode(true);
}

void Grid::actualize(sf::Event& event) {
    //  // japaneseCandles->UIEventHandler(event);
    //  // rain.actualize(); // de momento la rain me rompe la reactividad del sistema
    //  // crsor->actualize(); // Por ver
    //  // gridValues->actualize(*crsor);

    //  /* japaneseCandles.actualize();*/

    // if (spectatorModeOn) {
    //     // auto now = std::chrono::steady_clock::now();
    //     // // Verificar si han pasado 1000 ms desde la última actualización
    //     // if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count() >= 1000) {
    //     //     gridTime.spectatorModeActualize();
    //     //     gridValues.spectatorModeActualize(japaneseCandles);
    //     //     japaneseCandles.spectatorModeActualize(gridTime);
    //     //     lastUpdate = now;
    //     // }
    // } else {
    //     // Movimiento del Grid
    //     if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    //         isDragging = true;
    //         lastMousePos = sf::Vector2f(event.mouseButton.x, event.mouseButton.y);
    //     } else {
    //         if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
    //             isDragging = false;
    //         }
    //     }

    //     if (isDragging && event.type == sf::Event::MouseMoved) {
    //         sf::Vector2f currentMousePos = sf::Vector2f(event.mouseMove.x, event.mouseMove.y);
    //         sf::Vector2f delta = currentMousePos - lastMousePos;
    //         if (delta.x != 0) {
    //             gridTime->displacement(delta.x);
    //         }
    //         if (delta.y != 0) {
    //             gridValues->displacement(delta.y);
    //         }
    //         if (delta.y != 0 || delta.x != 0) {
    //             japaneseCandles->displacement();
    //             japaneseCandles->actualize();
    //         }
    //         lastMousePos = currentMousePos;
    //     }

    //     // Zoom del Grid
    //     if (event.type == sf::Event::EventType::KeyPressed && zoomClock.getElapsedTime().asMilliseconds() > 250) {
    //         switch (event.key.code) {
    //         case sf::Keyboard::Left:
    //             gridValues->zoomOut();
    //             japaneseCandles->actualize();
    //             zoomClock.restart();
    //             break;
    //         case sf::Keyboard::Right:
    //             gridValues->zoomIn();
    //             japaneseCandles->actualize();
    //             zoomClock.restart();
    //             break;
    //         default:
    //             break;
    //         }
    //     }
    // }
}

void Grid::draw(sf::RenderWindow& w) {
    // rain.draw(w);
    // gridTime->draw(w);
    // gridValues->draw();
    // japaneseCandles->draw(w);
    // userInterface.draw(w); // NEED TO IMPLEMENT IT ON THE RENDER MANAGER
    // crsor->draw();
}

Grid::~Grid() {
    delete japaneseCandles;
}

} // namespace ui
