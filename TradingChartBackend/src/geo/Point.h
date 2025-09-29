#pragma once
//#ifndef POINT_H
//#define POINT_H
//#include "PointService.h"
//#include "GridTime.h"
//#include "GridValues.h"
//#include "Timestamp.h"
//#include <SFML/Graphics.hpp>
//#include <string>
//
//class PointService;
//class GridValues;
//class GridTime;
//
//using namespace std;
//
//class Point{
//	sf::Vector2f pos;
//	float value = 0;
//	Timestamp timestamp;
//	PointService& service;
//public:
//	Point(Timestamp timestmp, PointService& pointService);
//	void actualize();
//	string getDateTime();
//	float getValue();
//	void setValue(float newValue);
//	float getPosX();
//	void actualize(GridTime& gridTime, GridValues& gridValues);
//	Point& operator+=(int n);
//	Point& operator-=(int n);
//	void actualizeValues();
//	void draw(sf::RenderWindow& w);
//
//
//};
//
//#endif