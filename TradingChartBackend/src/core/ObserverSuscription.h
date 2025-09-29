#pragma once

#include <cstddef>

#include "core/ICacheObserver.h"

namespace core {

struct ObserverSubscription {
    ICacheObserver* observer;
    std::size_t index;  // Índice de la caché al que se suscribe
};

}  // namespace core

