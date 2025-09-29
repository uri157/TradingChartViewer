//DIContainer.cpp
#include "bootstrap/DIContainer.h"

namespace bootstrap {

DIContainer* DIContainer::globalInstance = nullptr;
int DIContainer::nextScopeId = 0;

void DIContainer::setGlobalInstance(DIContainer* instance) {
    globalInstance = instance;
}

DIContainer& DIContainer::getGlobalInstance() {
    if (!globalInstance) throw std::runtime_error("Global DIContainer not set!");
    return *globalInstance;
}

int DIContainer::generateScopeId() {
    return nextScopeId++;
}

}  // namespace bootstrap

