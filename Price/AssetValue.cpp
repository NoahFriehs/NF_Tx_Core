
#include "AssetValue.h"
#include "StaticPrices.h"
#include "../FileLog.h"
#include <algorithm>

AssetValue::AssetValue() : isConnected(true), isRunning(true) {}

class StaticPrices;

double AssetValue::getPrice(const std::string &symbol) const {
    // The price hierarchy, pinned in one place and identical offline and
    // online: fresh (5-minute TTL) -> stale (any age) -> static baseline
    // -> 0.0. The stale layer beats the static table: an expired-but-real
    // market price is strictly more accurate than the baseline, so a TTL
    // lapse must never drop a known symbol to its baseline value.
    if (cache.testCache(symbol)) {
        return cache.checkCache(symbol);
    }

    // getStale returns the last stored price of any age; -1.0 = never
    // cached. A stored 0.0 (the endpoint's explicit "no value" answer)
    // counts as the stale entry and beats the static table: a symbol the
    // provider says is priceless keeps pricing at 0.0, not at an invented
    // baseline.
    const double stale = cache.getStale(symbol);
    if (stale >= 0.0) {
        return stale;
    }

    StaticPrices staticPrices;

    // find() instead of operator[]: the static table is a read-only fallback
    // (unknown symbols price at 0.0, same as the old operator[] default).
    auto it = staticPrices.prices.find(symbol);
    return it != staticPrices.prices.end() ? it->second : 0.0;
}

void AssetValue::ageCacheEntryForTest(const std::string &symbol, long long ageSeconds) {
    cache.ageEntryForTest(symbol, ageSeconds);
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


