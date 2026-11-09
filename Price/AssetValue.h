
#ifndef NF_TX_CORE_ASSETVALUE_H
#define NF_TX_CORE_ASSETVALUE_H


#include <string>
#include <vector>
#include <stdexcept>
#include "PriceCache.h"

// Asset value class
class AssetValue {
private:
    PriceCache cache;
    bool isConnected;
    bool isRunning;

public:
    AssetValue();

    //! Get the price of a symbol, hierarchy: fresh (5-minute TTL) ->
    //! stale (any age) -> static baseline -> 0.0 (unknown)
    double getPrice(const std::string &symbol) const;

    //! Test seam: age a cached entry [ageSeconds] into the past (the
    //! suite cannot sleep out the 5-minute TTL). No-op when uncached.
    void ageCacheEntryForTest(const std::string &symbol, long long ageSeconds);

    //! Load the cache with data
    void
    loadCacheWithData(const std::vector<std::string> &symbols, const std::vector<double> &prices);
};


#endif //NF_TX_CORE_ASSETVALUE_H
