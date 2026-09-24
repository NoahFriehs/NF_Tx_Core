
#include "PriceCache.h"

double PriceCache::checkCache(const std::string &symbol) {
    auto it = cache.find(symbol);
    if (it == cache.end()) {
        return -1.0;
    }
    if (it->second.isOlderThanFiveMinutes()) {
        // Handle cache expiration
        cache.erase(it);
        return -1.0;
    }
    return it->second.getPrice();
}

bool PriceCache::testCache(const std::string &symbol) {
    auto it = cache.find(symbol);
    return it != cache.end() && !it->second.isOlderThanFiveMinutes();
}

void PriceCache::addPrice(const std::string &symbol, double price) {
    // insert() does not replace an existing entry - update in place.
    auto it = cache.find(symbol);
    if (it != cache.end()) {
        it->second = Cache(symbol, price);
    } else {
        cache.emplace(symbol, Cache(symbol, price));
    }
}
