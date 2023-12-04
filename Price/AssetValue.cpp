//
// Created by nfriehs on 11/19/23.
//

#include "AssetValue.h"
#include "StaticPrices.h"

AssetValue::AssetValue() : isConnected(true), isRunning(true) {}

class StaticPrices;

double AssetValue::getPrice(const std::string &symbol) {
    // Implementation details for getting price

    if (cache.testCache(symbol)) {
        return cache.checkCache(symbol);
    }

    StaticPrices staticPrices;

    return staticPrices.prices[symbol];
}

void AssetValue::loadCache(const std::vector<std::string> &symbols) {
    // Implementation details for loading cache
    throw std::runtime_error("Not implemented");
}

void AssetValue::loadCacheWithData(const std::vector<std::string> &symbols,
                                   const std::vector<double> &prices) {
    // Implementation details for loading cache with data
    int len = symbols.size();
    for (int i = 0; i < len; i++) {
        cache.addPrice(symbols[i], prices[i]);
    }
}


