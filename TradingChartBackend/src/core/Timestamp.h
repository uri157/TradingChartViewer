#pragma once

#if 0
// TODO: legacy-ui
#pragma once


#include <chrono>
#include <memory>
#include <string>

namespace sf {
class RenderWindow;
}

namespace ui {
class RenderManager;
class ResourceProvider;
}

namespace core {

class Timestamp {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    sf::Text text;
    std::shared_ptr<sf::Font> font;
    // std::vector<sf::Vertex> verticalLine;
    long long timestamp = 0; // Timestamp en formato num�rico (milisegundos desde epoch)

    bool initialized = false;
    bool hasFont_ = false;
    bool ready_ = false;
    bool warningLogged_ = false;

    void updateText();
    void initialize(long long timestamp);
    std::chrono::system_clock::time_point toTimePoint() const;
    void fromTimePoint(const std::chrono::system_clock::time_point& tp);

    ui::RenderManager* renderManager = nullptr;
    sf::RenderWindow* w = nullptr;
    ui::ResourceProvider* resourceProvider = nullptr;

public:
    explicit Timestamp(long long timestamp = 0);

    // Operadores de comparación
    bool operator==(const Timestamp& other) const;
    bool operator!=(const Timestamp& other) const;
    bool operator<(const Timestamp& other) const;
    bool operator<=(const Timestamp& other) const;
    bool operator>(const Timestamp& other) const;
    bool operator>=(const Timestamp& other) const;

    // Operadores de asignación
    Timestamp& operator=(const Timestamp& other);
    Timestamp& operator=(long long timestamp);
    Timestamp& operator+=(const Timestamp& other);
    Timestamp& operator-=(const Timestamp& other);
    Timestamp& operator+=(int seconds);
    Timestamp& operator-=(int seconds);

    // Métodos adicionales
    std::string getString() const;
    long long getDifferenceInSeconds(const Timestamp& other) const;
    long long getSecondsSinceUnix() const;
    long long getTimestamp() const;

    // Dibujar con SFML
    void drawFullString();
    void draw();
    void setPosition(float x, float y);
    void setPositionX(float x);
    void setPositionY(float y);
    float getPositionX() const;
    float getPositionY() const;
    bool isReady() const;
};

}  // namespace core
























//#ifndef TIMESTAMP_H
//#define TIMESTAMP_H
//#include <iostream>
//#include <string>
//#include <iomanip>
//#include <sstream>
//#include <chrono>
//#include <memory>
//
//
//class Timestamp {
//    int year=0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
//    sf::Text text;
//    sf::Font font;
//    std::vector<sf::Vertex> verticalLine;
//
//    long long timestamp;
//    void updateText();
//    void initialize(long long timestamp);
//    std::chrono::system_clock::time_point toTimePoint() const;
//    void fromTimePoint(const std::chrono::system_clock::time_point& tp);
//
//public:
//    /*Timestamp(int year, int month, int day, int hour, int minute, int second);*/
//    Timestamp(long long timestamp);
//
//    // Operadores de comparaci�n
//    bool operator==(const Timestamp& other) const;
//    bool operator!=(const Timestamp& other) const;
//    bool operator<(const Timestamp& other) const;
//    bool operator<=(const Timestamp& other) const;
//    bool operator>(const Timestamp& other) const;
//    bool operator>=(const Timestamp& other) const;
//
//    // Operadores de asignaci�n
//    Timestamp& operator=(const Timestamp& other);
//    Timestamp& operator=(long long timestamp);
//    Timestamp& operator+=(const Timestamp& other);
//    Timestamp& operator-=(const Timestamp& other);
//    Timestamp& operator+=(int seconds);
//    Timestamp& operator-=(int seconds);
//
//    //long long getUnixTimestamp();
//    std::string getString() const;
//    long long getDifferenceInSeconds(const Timestamp& other) const;
//    long long getSecondsSinceUnix() const;
//    void drawFullString(sf::RenderWindow& w);
//    void setPosition(float x, float y);
//    void setPositionX(float x);
//    void setPositionY(float y);
//    float getPositionX() const;
//    float getPositionY() const;
//    long long getTimestamp();
//    bool operator<(long long unixTimestamp) const;
//
//    void draw(sf::RenderWindow& w);
//};
//
//
//#endif // TIMESTAMP_H

#endif  // legacy-ui

#include <chrono>
#include <string>

namespace core {

class Timestamp {
public:
    explicit Timestamp(long long timestamp = 0);

    bool operator==(const Timestamp& other) const;
    bool operator!=(const Timestamp& other) const;
    bool operator<(const Timestamp& other) const;
    bool operator<=(const Timestamp& other) const;
    bool operator>(const Timestamp& other) const;
    bool operator>=(const Timestamp& other) const;

    Timestamp& operator=(const Timestamp& other) = default;
    Timestamp& operator=(long long timestamp);
    Timestamp& operator+=(const Timestamp& other);
    Timestamp& operator-=(const Timestamp& other);
    Timestamp& operator+=(int seconds);
    Timestamp& operator-=(int seconds);

    std::string getString() const;
    long long getDifferenceInSeconds(const Timestamp& other) const;
    long long getSecondsSinceUnix() const;
    long long getTimestamp() const;

    void drawFullString();
    void draw();
    void setPosition(float x, float y);
    void setPositionX(float x);
    void setPositionY(float y);
    float getPositionX() const;
    float getPositionY() const;
    bool isReady() const;

private:
    void initialize(long long timestamp);

    long long timestamp_{0};
    float posX_{0.0f};
    float posY_{0.0f};
};

}  // namespace core
