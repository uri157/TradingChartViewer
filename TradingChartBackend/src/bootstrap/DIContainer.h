#pragma once

#include "config/Config.h"

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace bootstrap {

class DIContainerConfigurator;

class DIContainer {
public:
    enum class Lifetime { Singleton, Scoped, Transient };

private:
    struct ServiceEntry {
        Lifetime lifetime;
        std::function<void*()> factory;
        void* instance = nullptr;
    };

    std::unordered_map<std::string, ServiceEntry> services;
    std::unordered_map<int, std::unordered_map<std::string, void*>> scopedInstances;

    static DIContainer* globalInstance;

    static int nextScopeId;

    friend class DIContainerConfigurator;

    template <typename T>
    void registerServiceInternal(const std::string& name, Lifetime lifetime, std::function<T*()> factory);

    config::Config config_{};

public:
    static int generateScopeId();

    static void setGlobalInstance(DIContainer* instance);

    static DIContainer& getGlobalInstance();

    template <typename T>
    static T* resolve(const std::string& name, int scopeId = -1);

    void setConfig(const config::Config& config) { config_ = config; }
    const config::Config& getConfig() const { return config_; }

private:
    template <typename T>
    T* resolveInternal(const std::string& name, int scopeId = -1);
};

}  // namespace bootstrap

#include "bootstrap/DIContainer.tpp"

