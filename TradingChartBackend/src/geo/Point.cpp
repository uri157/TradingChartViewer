//#include "Point.h"
//
//Point::Point(Timestamp timestmp, PointService& pointService):timestamp(timestmp),service(pointService){
//	pos.x = pos.y = 0;
//	actualizeValues();
//}
//
//float Point::getValue() {
//	return value;
//}
//
//void Point::setValue(float newValue) {
//	value=newValue;
//}
//
//float Point::getPosX() {
//	return pos.x;
//}
//
//void Point::actualize(GridTime& gridTime, GridValues& gridValues) {
//	pos.x = gridTime.getValuePosX(timestamp.getSecondsSinceUnix() / 60);
//	pos.y = gridValues.getValuePosY(value);
//}
//
//
//Point& Point::operator+=(int n) {
//	timestamp += n;
//	actualizeValues();
//	return *this;
//}
//
//Point& Point::operator-=(int n) {
//	timestamp -= n;
//	actualizeValues();
//	return *this;
//}
//
//void Point::actualizeValues() {
//	service.actualizePoint(*this);
//}
//
//string Point::getDateTime() {
//	return timestamp.getString();
//}
//
//void Point::draw(sf::RenderWindow& w) {
//
//}