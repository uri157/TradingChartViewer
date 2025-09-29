#pragma once

#if 0
// TODO: legacy-ui

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/RenderSnapshot.h"

namespace core {

class EventBus {
public:
    using Callback = std::function<void(const sf::Event&)>;
    using KeyCallback = std::function<void()>;

    struct SeriesUpdated {
        long long firstOpen{0};
        long long lastOpen{0};
        std::size_t count{0};
        bool lastClosed{true};
        std::optional<std::uint64_t> tailHash{};
        std::optional<UiDataState> state{};
    };

    using SeriesUpdatedCallback = std::function<void(const SeriesUpdated&)>;

    struct Subscription {
        enum class Kind { None, Event, Key, Series };

        EventBus* bus{nullptr};
        Kind kind{Kind::None};
        sf::Event::EventType eventType{};
        sf::Keyboard::Key key{};
        std::size_t id{0};
        bool alive{false};

        Subscription() = default;
        Subscription(EventBus* b, sf::Event::EventType type, std::size_t identifier)
            : bus(b), kind(Kind::Event), eventType(type), id(identifier), alive(true) {}

        static Subscription makeKey(EventBus* b, sf::Keyboard::Key key, std::size_t identifier) {
            Subscription sub;
            sub.bus = b;
            sub.kind = Kind::Key;
            sub.key = key;
            sub.id = identifier;
            sub.alive = true;
            return sub;
        }

        static Subscription makeSeries(EventBus* b, std::size_t identifier) {
            Subscription sub;
            sub.bus = b;
            sub.kind = Kind::Series;
            sub.id = identifier;
            sub.alive = true;
            return sub;
        }

        ~Subscription() { reset(); }

        Subscription(Subscription&& other) noexcept { *this = std::move(other); }
        Subscription& operator=(Subscription&& other) noexcept {
            if (this != &other) {
                reset();
                bus = other.bus;
                kind = other.kind;
                eventType = other.eventType;
                key = other.key;
                id = other.id;
                alive = other.alive;
                other.bus = nullptr;
                other.kind = Kind::None;
                other.id = 0;
                other.alive = false;
            }
            return *this;
        }

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        void reset() {
            if (!alive || bus == nullptr) {
                return;
            }
            switch (kind) {
            case Kind::Event:
                bus->unsubscribe(eventType, id);
                break;
            case Kind::Key:
                bus->unsubscribeKey(key, id);
                break;
            case Kind::Series:
                bus->unsubscribeSeries(id);
                break;
            case Kind::None:
                break;
            }
            bus = nullptr;
            kind = Kind::None;
            id = 0;
            alive = false;
        }
    };

    Subscription subscribe(sf::Event::EventType type, Callback callback);
    Subscription subscribeKey(sf::Keyboard::Key key, KeyCallback callback);
    Subscription subscribeSeriesUpdated(SeriesUpdatedCallback callback);

    void unsubscribe(sf::Event::EventType type, std::size_t id);
    void unsubscribeKey(sf::Keyboard::Key key, std::size_t id);
    void unsubscribeSeries(std::size_t id);

    // Publicar eventos
    void publish(const sf::Event &event);
    void publishSeriesUpdated(const SeriesUpdated& event);
    bool consumeSeriesChanged();
    void clearAll();

private:
    struct CallbackData {
        std::size_t id{};
        Callback callback{};
    };

    struct KeyCallbackData {
        std::size_t id{};
        KeyCallback callback{};
    };

    struct SeriesCallbackData {
        std::size_t id{};
        SeriesUpdatedCallback callback{};
    };

    std::unordered_map<sf::Event::EventType, std::vector<CallbackData>> listeners;
    std::unordered_map<sf::Keyboard::Key, std::vector<KeyCallbackData>> keyListeners;
    std::vector<SeriesCallbackData> seriesListeners_;
    std::optional<SeriesUpdated> lastSeriesEvent_;
    std::atomic<bool> seriesChanged_{false};
    std::size_t nextId_ = 1;
};

}  // namespace core

#endif  // legacy-ui

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "core/RenderSnapshot.h"

namespace core {

class EventBus {
public:
    struct SeriesUpdated {
        long long firstOpen{0};
        long long lastOpen{0};
        std::size_t count{0};
        bool lastClosed{true};
        std::optional<std::uint64_t> tailHash{};
        std::optional<UiDataState> state{};
    };

    using SeriesUpdatedCallback = std::function<void(const SeriesUpdated&)>;

    class Subscription {
    public:
        Subscription() = default;
        Subscription(EventBus* bus, std::size_t id);
        ~Subscription();

        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        void reset();

    private:
        EventBus* bus_{nullptr};
        std::size_t id_{0};
    };

    Subscription subscribeSeriesUpdated(SeriesUpdatedCallback callback);
    void unsubscribeSeries(std::size_t id);

    void publishSeriesUpdated(const SeriesUpdated& event);
    bool consumeSeriesChanged();
    void clearAll();

private:
    struct SeriesCallbackData {
        std::size_t id{};
        SeriesUpdatedCallback callback{};
    };

    std::vector<SeriesCallbackData> seriesListeners_;
    std::optional<SeriesUpdated> lastSeriesEvent_;
    std::atomic<bool> seriesChanged_{false};
    std::size_t nextId_{1};
};

}  // namespace core
