#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Vertex.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <chrono>
#include <limits>
#include <memory>
#include <vector>

#include "core/EventBus.h"
#include "core/IPriceLimitObserver.h"

// Forward declarations para evitar headers pesados en el .h
namespace infra { namespace storage { class DatabaseEngine; } }

namespace ui {

class Cursor;
class GridValue;
class GridMouseValue;
class GridJapaneseCandle;
class ResourceProvider;

class GridValues : public core::IPriceLimitObserver {
    float minRealValue,
          jumpValue = 0,
          minVal    = std::numeric_limits<float>::lowest(),
          maxVal    = std::numeric_limits<float>::max();

    infra::storage::DatabaseEngine* db = nullptr;
    Cursor*                         cursor = nullptr;
    sf::RenderWindow*               w = nullptr;
    core::EventBus*                 eventBus = nullptr;
    core::EventBus::CallbackID      uiEventHandlerToken{};

    int   rangeInCents = 0;
    int   scopeId      = 0;
    long double centsperpixel = 0;

    bool spectatorModeOn = false;
    bool isDragging      = false;

    std::vector<std::shared_ptr<GridValue>> values;
    GridMouseValue*   gridMouseValue = nullptr;

    std::chrono::steady_clock::time_point lastUpdate;
    // sf::Clock zoomClock; // si se usa, descomentar

    sf::Vector2f lastMousePos;
    sf::Vertex   valuesVerticalLine[2]; // Línea vertical que divide a los valores del grid

    ResourceProvider* resourceProvider = nullptr;

public:
    GridValues(int scopeId);

    void spectatorMode(bool onOf);
    /*void setMaxMinValue(float minValue, float maxValue);*/
    void redefineValuesVector();
    void draw();
    // void actualize();
    /*void spectatorModeActualize(GridJapaneseCandle& japaneseCandles);*/
    float  getValuePosY(float value);            // posición Y para un valor dado
    float  getValueFromPositionOnY(float position); // valor a partir de una posición Y
    void   displacement(float deltaY);
    void   zoomOut();
    void   zoomIn();
    double calculateMemoryUsage();

    // IPriceLimitObserver
    void onMaxPriceLimitChanged(double newMax) override;
    void onMinPriceLimitChanged(double newMin) override;

    ~GridValues();
};

} // namespace ui
