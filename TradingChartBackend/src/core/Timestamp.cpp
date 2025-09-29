#if 0
// TODO: legacy-ui
#include "core/Timestamp.h"

#include <array>
#include <cstdio>
#include <ctime>

#include "bootstrap/DIContainer.h"
#include "logging/Log.h"
#include "core/TimeUtils.h"
#include "ui/RenderManager.h"
#include "ui/ResourceProvider.h"


namespace core {

// Constructor
Timestamp::Timestamp(long long timestamp)
    : renderManager(bootstrap::DIContainer::resolve<ui::RenderManager>("RenderManager")),
      w(bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow")),
      resourceProvider(bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider")) {
    if (resourceProvider) {
        auto fontResource = resourceProvider->getFontResource("ui");
        font = fontResource.font;
        hasFont_ = fontResource.ready;
        if (!hasFont_ && !warningLogged_) {
            LOG_WARN(logging::LogCategory::UI, "Timestamp text disabled: font could not be loaded.");
            warningLogged_ = true;
        }
        if (hasFont_ && font) {
            text.setFont(*font);
        }
    }
    text.setCharacterSize(18);
    text.setFillColor(sf::Color(200, 205, 220));
    initialize(timestamp);
    initialized=true;
    ready_ = hasFont_;
}

// Inicializar con timestamp
void Timestamp::initialize(long long timestamp) {
    this->timestamp = timestamp;
    auto seconds = std::chrono::seconds(timestamp / TimeUtils::kMillisPerSecond);
    auto time_point = std::chrono::system_clock::time_point(seconds);
    fromTimePoint(time_point);
    updateText();
}

// Convertir a time_point
std::chrono::system_clock::time_point Timestamp::toTimePoint() const {
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    auto time = timegm(&tm);
    return std::chrono::system_clock::from_time_t(time);
}

// Convertir desde time_point
void Timestamp::fromTimePoint(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = {};
    gmtime_r(&time, &tm);
    year = tm.tm_year + 1900;
    month = tm.tm_mon + 1;
    day = tm.tm_mday;
    hour = tm.tm_hour;
    minute = tm.tm_min;
    second = tm.tm_sec;
    updateText();
}

// Actualizar texto para SFML
void Timestamp::updateText() {
    std::array<char, 6> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%02d:%02d", hour, minute);
    text.setString(buffer.data());
    sf::FloatRect textRect = text.getLocalBounds();
    text.setOrigin(textRect.left + textRect.width / 2.0f, textRect.top + textRect.height / 2.0f);
    if(initialized && isReady()) draw();
}

// Operadores de comparaci�n
bool Timestamp::operator==(const Timestamp& other) const {
    return timestamp == other.timestamp;
}

bool Timestamp::operator!=(const Timestamp& other) const {
    return !(*this == other);
}

bool Timestamp::operator<(const Timestamp& other) const {
    return timestamp < other.timestamp;
}

bool Timestamp::operator<=(const Timestamp& other) const {
    return timestamp <= other.timestamp;
}

bool Timestamp::operator>(const Timestamp& other) const {
    return timestamp > other.timestamp;
}

bool Timestamp::operator>=(const Timestamp& other) const {
    return timestamp >= other.timestamp;
}

// Operadores de asignaci�n
Timestamp& Timestamp::operator=(const Timestamp& other) {
    if (this != &other) {
        year = other.year;
        month = other.month;
        day = other.day;
        hour = other.hour;
        minute = other.minute;
        second = other.second;
        timestamp = other.timestamp;
    }
    updateText();
    return *this;
}

Timestamp& Timestamp::operator=(long long timestamp) {
    initialize(timestamp);
    return *this;
}

Timestamp& Timestamp::operator+=(const Timestamp& other) {
    timestamp += other.timestamp;
    initialize(timestamp);
    return *this;
}

Timestamp& Timestamp::operator-=(const Timestamp& other) {
    timestamp -= other.timestamp;
    initialize(timestamp);
    return *this;
}

Timestamp& Timestamp::operator+=(int seconds) {
    timestamp += seconds * TimeUtils::kMillisPerSecond;
    initialize(timestamp);
    return *this;
}

Timestamp& Timestamp::operator-=(int seconds) {
    timestamp -= seconds * TimeUtils::kMillisPerSecond;
    initialize(timestamp);
    return *this;
}

// Obtener string representativo
std::string Timestamp::getString() const {
    std::ostringstream oss;
    oss << year << "-" << std::setw(2) << std::setfill('0') << month << "-"
        << std::setw(2) << std::setfill('0') << day << " "
        << std::setw(2) << std::setfill('0') << hour << ":"
        << std::setw(2) << std::setfill('0') << minute << ":"
        << std::setw(2) << std::setfill('0') << second;
    return oss.str();
}

long long Timestamp::getDifferenceInSeconds(const Timestamp& other) const {
    return (timestamp - other.timestamp) / 1000;
}

long long Timestamp::getSecondsSinceUnix() const {
    return timestamp / 1000;
}

long long Timestamp::getTimestamp() const {
    return timestamp;
}

// Dibujar con SFML
void Timestamp::drawFullString() {
    // w->draw(text);
}

void Timestamp::draw() {
    if (!renderManager) {
        return;
    }
    if (!isReady()) {
        return;
    }

    sf::Text textCopy = text;
    auto fontCopy = font;
    if (fontCopy) {
        textCopy.setFont(*fontCopy);
    }

    renderManager->addRenderCommand(ui::PanelId::BottomAxis,
                                    ui::LayerId::Labels,
                                    [textCopy, fontCopy](sf::RenderTarget& target) mutable {
        if (!fontCopy) {
            return;
        }
        target.draw(textCopy, sf::RenderStates::Default);
    });
}

void Timestamp::setPosition(float x, float y) {
    text.setPosition(x, y);
}

void Timestamp::setPositionX(float x) {
    auto pos = text.getPosition();
    text.setPosition(x, pos.y);
}

void Timestamp::setPositionY(float y) {
    auto pos = text.getPosition();
    text.setPosition(pos.x, y);
}

float Timestamp::getPositionX() const {
    return text.getPosition().x;
}

float Timestamp::getPositionY() const {
    return text.getPosition().y;
}

bool Timestamp::isReady() const {
    return ready_ && hasFont_;
}

}  // namespace core

#endif  // legacy-ui

#include "core/Timestamp.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {
std::tm toUtc(long long timestampMs) {
    const std::time_t seconds = static_cast<std::time_t>(timestampMs / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    return tm;
}
}  // namespace

namespace core {

Timestamp::Timestamp(long long timestamp) {
    initialize(timestamp);
}

void Timestamp::initialize(long long timestamp) {
    timestamp_ = timestamp;
}

Timestamp& Timestamp::operator=(long long timestamp) {
    initialize(timestamp);
    return *this;
}

Timestamp& Timestamp::operator+=(const Timestamp& other) {
    timestamp_ += other.timestamp_;
    return *this;
}

Timestamp& Timestamp::operator-=(const Timestamp& other) {
    timestamp_ -= other.timestamp_;
    return *this;
}

Timestamp& Timestamp::operator+=(int seconds) {
    timestamp_ += static_cast<long long>(seconds) * 1000LL;
    return *this;
}

Timestamp& Timestamp::operator-=(int seconds) {
    timestamp_ -= static_cast<long long>(seconds) * 1000LL;
    return *this;
}

bool Timestamp::operator==(const Timestamp& other) const {
    return timestamp_ == other.timestamp_;
}

bool Timestamp::operator!=(const Timestamp& other) const {
    return !(*this == other);
}

bool Timestamp::operator<(const Timestamp& other) const {
    return timestamp_ < other.timestamp_;
}

bool Timestamp::operator<=(const Timestamp& other) const {
    return timestamp_ <= other.timestamp_;
}

bool Timestamp::operator>(const Timestamp& other) const {
    return timestamp_ > other.timestamp_;
}

bool Timestamp::operator>=(const Timestamp& other) const {
    return timestamp_ >= other.timestamp_;
}

std::string Timestamp::getString() const {
    std::tm tm = toUtc(timestamp_);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

long long Timestamp::getDifferenceInSeconds(const Timestamp& other) const {
    return (timestamp_ - other.timestamp_) / 1000LL;
}

long long Timestamp::getSecondsSinceUnix() const {
    return timestamp_ / 1000LL;
}

long long Timestamp::getTimestamp() const {
    return timestamp_;
}

void Timestamp::drawFullString() {}

void Timestamp::draw() {}

void Timestamp::setPosition(float x, float y) {
    posX_ = x;
    posY_ = y;
}

void Timestamp::setPositionX(float x) {
    posX_ = x;
}

void Timestamp::setPositionY(float y) {
    posY_ = y;
}

float Timestamp::getPositionX() const {
    return posX_;
}

float Timestamp::getPositionY() const {
    return posY_;
}

bool Timestamp::isReady() const {
    return true;
}

}  // namespace core
