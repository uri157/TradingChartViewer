#if 0
// TODO: legacy-ui
#include "core/EventBus.h"
#include <algorithm>

#ifdef DIAG_SYNC
#include "core/LogUtils.h"
#include "logging/Log.h"
#endif

#ifdef TTP_ENABLE_DIAG
#include "core/Diag.h"
#endif

#ifdef DIAG_SYNC
namespace {
const char* diagUiDataStateLabel(const std::optional<core::UiDataState>& state) {
    if (!state.has_value()) {
        return "None";
    }
    switch (*state) {
    case core::UiDataState::Loading:
        return "Loading";
    case core::UiDataState::LiveOnly:
        return "LiveOnly";
    case core::UiDataState::Ready:
        return "Ready";
    }
    return "Unknown";
}
} // namespace
#endif

namespace core {

EventBus::Subscription EventBus::subscribe(sf::Event::EventType type, Callback callback) {
    const std::size_t id = nextId_++;
    auto& vec = listeners[type];
    vec.push_back(CallbackData{id, std::move(callback)});
    return Subscription(this, type, id);
}

EventBus::Subscription EventBus::subscribeKey(sf::Keyboard::Key key, KeyCallback callback) {
    const std::size_t id = nextId_++;
    auto& vec = keyListeners[key];
    vec.push_back(KeyCallbackData{id, std::move(callback)});
    return Subscription::makeKey(this, key, id);
}

EventBus::Subscription EventBus::subscribeSeriesUpdated(SeriesUpdatedCallback callback) {
    const std::size_t id = nextId_++;
    seriesListeners_.push_back(SeriesCallbackData{id, std::move(callback)});
    return Subscription::makeSeries(this, id);
}

void EventBus::unsubscribe(sf::Event::EventType type, std::size_t id) {
    auto it = listeners.find(type);
    if (it == listeners.end()) {
        return;
    }
    auto& vec = it->second;
    for (std::size_t idx = 0; idx < vec.size(); ++idx) {
        if (vec[idx].id == id) {
            if (idx + 1 != vec.size()) {
                vec[idx] = std::move(vec.back());
            }
            vec.pop_back();
            if (vec.empty()) {
                listeners.erase(it);
            }
            return;
        }
    }
}

void EventBus::unsubscribeKey(sf::Keyboard::Key key, std::size_t id) {
    auto it = keyListeners.find(key);
    if (it == keyListeners.end()) {
        return;
    }
    auto& vec = it->second;
    for (std::size_t idx = 0; idx < vec.size(); ++idx) {
        if (vec[idx].id == id) {
            if (idx + 1 != vec.size()) {
                vec[idx] = std::move(vec.back());
            }
            vec.pop_back();
            if (vec.empty()) {
                keyListeners.erase(it);
            }
            return;
        }
    }
}

void EventBus::unsubscribeSeries(std::size_t id) {
    for (std::size_t idx = 0; idx < seriesListeners_.size(); ++idx) {
        if (seriesListeners_[idx].id == id) {
            if (idx + 1 != seriesListeners_.size()) {
                seriesListeners_[idx] = std::move(seriesListeners_.back());
            }
            seriesListeners_.pop_back();
            return;
        }
    }
}

void EventBus::publish(const sf::Event &event) {
    if (auto it = listeners.find(event.type); it != listeners.end()) {
        auto& vec = it->second;
        for (std::size_t i = 0; i < vec.size();) {
            const std::size_t id = vec[i].id;
            auto callback = vec[i].callback;
            if (callback) {
                callback(event);
            }
            if (i < vec.size() && vec[i].id == id) {
                ++i;
            }
        }
    }

    if (event.type == sf::Event::KeyPressed) {
        if (auto it = keyListeners.find(event.key.code); it != keyListeners.end()) {
            auto& vec = it->second;
            for (std::size_t i = 0; i < vec.size();) {
                const std::size_t id = vec[i].id;
                auto callback = vec[i].callback;
                if (callback) {
                    callback();
                }
                if (i < vec.size() && vec[i].id == id) {
                    ++i;
                }
            }
        }
    }
}

void EventBus::publishSeriesUpdated(const SeriesUpdated& event) {
    if (lastSeriesEvent_ && lastSeriesEvent_->firstOpen == event.firstOpen &&
        lastSeriesEvent_->lastOpen == event.lastOpen && lastSeriesEvent_->count == event.count &&
        lastSeriesEvent_->lastClosed == event.lastClosed &&
        lastSeriesEvent_->tailHash == event.tailHash && lastSeriesEvent_->state == event.state) {
#ifdef DIAG_SYNC
        static core::LogRateLimiter diagSeriesSuppressedLimiter{std::chrono::milliseconds(150)};
        if (diagSeriesSuppressedLimiter.allow()) {
            const unsigned long long tailHashValue = event.tailHash.has_value()
                ? static_cast<unsigned long long>(*event.tailHash)
                : 0ull;
            LOG_INFO(logging::LogCategory::DATA,
                     "DIAG_SYNC series_update suppressed count=%zu firstOpen=%lld lastOpen=%lld lastClosed=%d tailHashPresent=%d tailHash=0x%llx state=%s",
                     event.count,
                     static_cast<long long>(event.firstOpen),
                     static_cast<long long>(event.lastOpen),
                     event.lastClosed ? 1 : 0,
                     event.tailHash.has_value() ? 1 : 0,
                     tailHashValue,
                     diagUiDataStateLabel(event.state));
        }
#endif
        return;
    }
    lastSeriesEvent_ = event;
    seriesChanged_.exchange(true, std::memory_order_acq_rel);
#ifdef TTP_ENABLE_DIAG
    auto diagTimer = core::diag::timer("bus.series");
    core::diag::incr("bus.series.published");
    core::diag::incr("bus.series.listeners", static_cast<std::uint64_t>(seriesListeners_.size()));
#endif
#ifdef DIAG_SYNC
    {
        static core::LogRateLimiter diagSeriesPublishLimiter{std::chrono::milliseconds(120)};
        if (diagSeriesPublishLimiter.allow()) {
            const unsigned long long tailHashValue = event.tailHash.has_value()
                ? static_cast<unsigned long long>(*event.tailHash)
                : 0ull;
            LOG_INFO(logging::LogCategory::DATA,
                     "DIAG_SYNC series_update publish count=%zu firstOpen=%lld lastOpen=%lld lastClosed=%d tailHashPresent=%d tailHash=0x%llx state=%s",
                     event.count,
                     static_cast<long long>(event.firstOpen),
                     static_cast<long long>(event.lastOpen),
                     event.lastClosed ? 1 : 0,
                     event.tailHash.has_value() ? 1 : 0,
                     tailHashValue,
                     diagUiDataStateLabel(event.state));
        }
    }
#endif
    for (std::size_t i = 0; i < seriesListeners_.size();) {
        const std::size_t id = seriesListeners_[i].id;
        auto callback = seriesListeners_[i].callback;
        if (callback) {
            callback(event);
        }
        if (i < seriesListeners_.size() && seriesListeners_[i].id == id) {
            ++i;
        }
    }
}

bool EventBus::consumeSeriesChanged() {
    return seriesChanged_.exchange(false, std::memory_order_acq_rel);
}

void EventBus::clearAll() {
    listeners.clear();
    keyListeners.clear();
    seriesListeners_.clear();
    lastSeriesEvent_.reset();
    seriesChanged_.store(false, std::memory_order_release);
    nextId_ = 1;
}

}  // namespace core

#endif  // legacy-ui

#include "core/EventBus.h"

#include <utility>

namespace core {

EventBus::Subscription::Subscription(EventBus* bus, std::size_t id)
    : bus_(bus), id_(id) {}

EventBus::Subscription::~Subscription() {
    reset();
}

EventBus::Subscription::Subscription(Subscription&& other) noexcept {
    bus_ = other.bus_;
    id_ = other.id_;
    other.bus_ = nullptr;
    other.id_ = 0;
}

EventBus::Subscription& EventBus::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        bus_ = other.bus_;
        id_ = other.id_;
        other.bus_ = nullptr;
        other.id_ = 0;
    }
    return *this;
}

void EventBus::Subscription::reset() {
    if (bus_ && id_ != 0) {
        bus_->unsubscribeSeries(id_);
    }
    bus_ = nullptr;
    id_ = 0;
}

EventBus::Subscription EventBus::subscribeSeriesUpdated(SeriesUpdatedCallback callback) {
    const std::size_t id = nextId_++;
    seriesListeners_.push_back(SeriesCallbackData{id, std::move(callback)});
    return Subscription(this, id);
}

void EventBus::unsubscribeSeries(std::size_t id) {
    for (std::size_t idx = 0; idx < seriesListeners_.size(); ++idx) {
        if (seriesListeners_[idx].id == id) {
            if (idx + 1 != seriesListeners_.size()) {
                seriesListeners_[idx] = std::move(seriesListeners_.back());
            }
            seriesListeners_.pop_back();
            break;
        }
    }
}

void EventBus::publishSeriesUpdated(const SeriesUpdated& event) {
    if (lastSeriesEvent_ && lastSeriesEvent_->firstOpen == event.firstOpen &&
        lastSeriesEvent_->lastOpen == event.lastOpen && lastSeriesEvent_->count == event.count &&
        lastSeriesEvent_->lastClosed == event.lastClosed &&
        lastSeriesEvent_->tailHash == event.tailHash && lastSeriesEvent_->state == event.state) {
        return;
    }

    lastSeriesEvent_ = event;
    seriesChanged_.exchange(true, std::memory_order_acq_rel);

    for (std::size_t idx = 0; idx < seriesListeners_.size();) {
        const std::size_t listenerId = seriesListeners_[idx].id;
        auto callback = seriesListeners_[idx].callback;
        if (callback) {
            callback(event);
        }
        if (idx < seriesListeners_.size() && seriesListeners_[idx].id == listenerId) {
            ++idx;
        }
    }
}

bool EventBus::consumeSeriesChanged() {
    return seriesChanged_.exchange(false, std::memory_order_acq_rel);
}

void EventBus::clearAll() {
    seriesListeners_.clear();
    lastSeriesEvent_.reset();
    seriesChanged_.store(false, std::memory_order_release);
    nextId_ = 1;
}

}  // namespace core
