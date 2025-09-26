//DIContainer.tpp
#include "bootstrap/DIContainer.h"

namespace bootstrap {

// DIContainer* DIContainer::globalInstance = nullptr;

// void DIContainer::setGlobalInstance(DIContainer* instance) {
//     globalInstance = instance;
// }

// DIContainer& DIContainer::getGlobalInstance() {
//     if (!globalInstance) throw std::runtime_error("Global DIContainer not set!");
//     return *globalInstance;
// }


template <typename T>
void DIContainer::registerServiceInternal(const std::string& name, Lifetime lifetime, std::function<T*()> factory) {
    services[name] = {
        lifetime,
        [factory]() -> void* { return static_cast<void*>(factory()); }, // 🔥 Casting explícito a void*
        nullptr
    };
}

template <typename T>
T* DIContainer::resolveInternal(const std::string& name, int scopeId) {
    if (services.find(name) == services.end()) {
        throw std::runtime_error("Service not found: " + name);
    }

    ServiceEntry& entry = services[name];
    if (entry.lifetime == Lifetime::Singleton) {
        if (!entry.instance) entry.instance = entry.factory();
        return static_cast<T*>(entry.instance);
    }
    else if (entry.lifetime == Lifetime::Scoped) {
        if (scopedInstances[scopeId].find(name) == scopedInstances[scopeId].end()) {
            scopedInstances[scopeId][name] = entry.factory();
        }
        return static_cast<T*>(scopedInstances[scopeId][name]);
    }
    else { // Transient
        return static_cast<T*>(entry.factory());
    }
}

template <typename T>
T* DIContainer::resolve(const std::string& name, int scopeId) {
    return getGlobalInstance().resolveInternal<T>(name, scopeId);
}

}  // namespace bootstrap

