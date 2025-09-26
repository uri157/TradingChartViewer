#pragma once

#include "core/EventBus.h"
#include "ui/Cursor.h"
#include "ui/GridValue.h"

namespace ui {

class GridValues;

class GridMouseValue : public GridValue {
private:
    sf::RenderWindow* w;
    core::EventBus* eventBus;
    core::EventBus::CallbackID uiEventHandlerToken;
    GridValues* gridValues;
    Cursor* cursor;
    float mouseValue;

public:
    GridMouseValue(GridValues* gridValues);
    void actualize();
    using GridValue::operator=;
    ~GridMouseValue();
};

} // namespace ui

