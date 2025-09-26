//JapaneseCandle.h
#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ui/ResourceProvider.h"
#include "core/Timestamp.h"

namespace ui {

class JapaneseCandleService;

class JapaneseCandle {
public:
    explicit JapaneseCandle(core::Timestamp time);
    void setClosePriceHorizontalLinePosition(float newPosition);
    void drawCandle(sf::RenderTarget& window);
    void drawClosePriceIndicator(sf::RenderTarget& window);
    void setClosePrice(std::optional<double> val);
    void setOpenPrice(std::optional<double> val);
    void setMaxPrice(std::optional<double> val);
    void setMinPrice(std::optional<double> val);
    void setNewTime(core::Timestamp newTime);
    void setBodySize(sf::Vector2f newSize);
    void setBodyFillColor(sf::Color newColor);
    void setBodyPosition(sf::Vector2f newPosition);
    void setWickSize(sf::Vector2f newSize);
    void setWickFillColor(sf::Color newColor);
    void setWickPosition(sf::Vector2f newPosition);
    sf::Text& accessClosePriceText();

    void setBodyOrigin(sf::Vector2f newOrigin);
    void setWickOrigin(sf::Vector2f newOrigin);

    std::optional<double> getMaxPrice() const;
    std::optional<double> getMinPrice() const;
    std::optional<double> getClosePrice() const;
    std::optional<double> getOpenPrice() const;
  /*  float getPosX() const;*/
    const sf::RectangleShape& getBody() const;
    const sf::RectangleShape& getWick() const;
   /* std::string getDateTime() const;
    long long getTimestamp() const;*/
    core::Timestamp getTime() const;

    bool isReady() const;
    bool isClosePriceIndicatorReady() const;
    bool hasFont() const;
    void setDataReady(bool ready);
    void setClosePriceIndicatorReady(bool ready);
    void resetClosePriceIndicator();
    

    /*JapaneseCandle& operator+=(int n);
    JapaneseCandle& operator-=(int n);*/

private:
    /*void centerBodyAndWick();
    void initializeValues();*/

    // Member variables
    core::Timestamp time;
    std::optional<float> closePrice;
    std::optional<float> openPrice;
    std::optional<float> maxPrice;
    std::optional<float> minPrice;

    // Visual properties
    sf::RectangleShape body;
    sf::RectangleShape wick;
    sf::Text closePriceText;
    std::shared_ptr<sf::Font> font;
    ResourceProvider* resourceProvider = nullptr;
    std::vector<sf::Vertex> closePriceHorizontalLine;
    bool hasFont_{false};
    bool geometryReady_{false};
    bool closePriceIndicatorReady_{false};
    bool fontWarningLogged_{false};
};

}  // namespace ui
