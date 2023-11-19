//
// Created by nfriehs on 11/19/23.
//

#include "PriceCache.h"

double PriceCache::checkCache(const std::string &symbol) {
    if (testCache(symbol)) {
        Cache *cacheToCheck = &cache.find(symbol)->second;
        if (cacheToCheck->isOlderThanFiveMinutes()) {
            // Handle cache expiration
            cache.erase(symbol);
            return -1.0;
        }
        return cacheToCheck->getPrice();
    }
    return -1.0;
}

bool PriceCache::testCache(const std::string& symbol) {
    Cache *cacheToCheck = &cache.find(symbol)->second;
    return cache.find(symbol) != cache.end() && !cacheToCheck->isOlderThanFiveMinutes();
}

void PriceCache::addPrice(const std::string& symbol, double price) {
    auto* cacheToAdd = new Cache(symbol, price);
    cache.insert({symbol, *cacheToAdd});
}

