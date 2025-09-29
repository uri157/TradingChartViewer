#pragma once

#include <cstddef>

#include "core/IFullDataObserver.h"

namespace core {

struct FullDataObserverSubscription {
    IFullDataObserver* observer;
    std::size_t index;  // Índice de la caché al que se suscribe
};

}  // namespace core

