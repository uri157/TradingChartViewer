//#include "Cache.h"
//
//void Cache::addData(const PriceData& data) {
//    if (!cache.empty() && data.openTime <= cache.front().openTime) {
//        return; // Ignorar datos duplicados o más antiguos
//    }
//
//    cache.push_front(data);
//    if (cache.size() > cacheSize) {
//        cache.pop_back();
//    }
//
//    recalculateLimits();
//    notifyObservers();
//}
//
//void Cache::addObserver(ICacheObserver* observer) {
//    observers.push_back(observer);
//}
//
//void Cache::removeObserver(ICacheObserver* observer) {
//    observers.erase(std::remove(observers.begin(), observers.end(), observer), observers.end());
//}
//
//void Cache::addPriceLimitObserver(IPriceLimitObserver* observer) {
//    priceLimitObservers.push_back(observer);
//}
//
//void Cache::removePriceLimitObserver(IPriceLimitObserver* observer) {
//    priceLimitObservers.erase(std::remove(priceLimitObservers.begin(), priceLimitObservers.end(), observer), priceLimitObservers.end());
//}
//
//void Cache::notifyObservers() const {
//    for (const auto& observer : observers) {
//        observer->onCacheUpdated();
//    }
//}
//
//void Cache::notifyPriceLimitChanges() {
//    for (const auto& observer : priceLimitObservers) {
//        observer->onMaxPriceLimitChanged(currentMaxPrice);
//        observer->onMinPriceLimitChanged(currentMinPrice);
//    }
//}
//
//void Cache::recalculateLimits() {
//    currentMaxPrice = std::numeric_limits<double>::lowest();
//    currentMinPrice = std::numeric_limits<double>::max();
//
//    for (const auto& data : cache) {
//        currentMaxPrice = std::max(currentMaxPrice, data.highPrice);
//        currentMinPrice = std::min(currentMinPrice, data.lowPrice);
//    }
//
//    notifyPriceLimitChanges();
//}
