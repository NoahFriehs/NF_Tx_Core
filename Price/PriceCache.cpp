
#include "PriceCache.h"

// note: the readers are const (no erasure of stale entries - they simply
// report -1.0 / false and are replaced on the next addPrice)
double PriceCache::checkCache(const std::string &symbol) const {
    auto it = cache.find(symbol);
    if (it == cache.end()) {
        return -1.0;
    }
    if (it->second.isOlderThanFiveMinutes()) {
        // Handle cache expiration
        return -1.0;
    }
    return it->second.getPrice();
}

bool PriceCache::testCache(const std::string &symbol) const {
    auto it = cache.find(symbol);
    return it != cache.end() && !it->second.isOlderThanFiveMinutes();
}

double PriceCache::getStale(const std::string &symbol) const {
    // The stale layer of the price hierarchy: the last stored price
    // regardless of age. -1.0 doubles as the "never cached" sentinel
    // (a stored -1.0 price is unreachable - the pass only ever stores
    // endpoint prices, 0.0 = "no value", and live values are > 0).
    auto it = cache.find(symbol);
    return it != cache.end() ? it->second.getPrice() : -1.0;
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

void PriceCache::ageEntryForTest(const std::string &symbol, long long ageSeconds) {
    auto it = cache.find(symbol);
    if (it == cache.end()) {
        return;
    }
    it->second = Cache(symbol,
                       it->second.getPrice(),
                       std::chrono::system_clock::now() -
                               std::chrono::seconds(ageSeconds));
}
