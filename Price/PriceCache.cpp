//
// Created by nfriehs on 11/19/23.
//

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
    // Value-map: replace the entry instead of new + insert. The previous
    // version leaked one Cache per call and insert() would not update an
    // existing entry, leaving stale prices in the cache.
    auto it = cache.find(symbol);
    if (it != cache.end()) {
        it->second = Cache(symbol, price);
    } else {
        cache.emplace(symbol, Cache(symbol, price));
    }
}
