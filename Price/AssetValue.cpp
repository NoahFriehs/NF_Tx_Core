//
// Created by nfriehs on 11/19/23.
//

#include "AssetValue.h"
#include "StaticPrices.h"
#include "../FileLog.h"
#include <algorithm>

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


void AssetValue::loadCacheWithData(const std::vector<std::string> &symbols,
                                   const std::vector<double> &prices) {
    // Defensive: only pair up entries that exist in both vectors.
    const auto len = std::min(symbols.size(), prices.size());
    if (symbols.size() != prices.size()) {
        // No symbols (e.g. right after clearAll) is harmless; a real
        // mismatch between non-empty vectors is a bug in the caller.
        if (symbols.empty()) {
            FileLog::d("AssetValue",
                       "No currencies known, skipping price update for " +
                               std::to_string(prices.size()) + " prices");
        } else {
            FileLog::w("AssetValue",
                       "Symbol/price count mismatch (" + std::to_string(symbols.size()) +
                               " vs " + std::to_string(prices.size()) + ")");
        }
    }
    for (size_t i = 0; i < len; i++) {
        cache.addPrice(symbols[i], prices[i]);
    }
}


