//
// Created by nfriehs on 11/19/23.
//

#ifndef NF_TX_CORE_PRICECACHE_H
#define NF_TX_CORE_PRICECACHE_H

#include <unordered_map>
#include <string>
#include <chrono>
#include "Cache.h"

// Object to store prices for 5 minutes
class PriceCache {
private:
    std::unordered_map<std::string, Cache> cache = {};

public:
    //! Check the cache for a symbol
    double checkCache(const std::string &symbol);

    //! Check if the cache is valid for a symbol
    bool testCache(const std::string &symbol);

    //! Add a price to the cache
    void addPrice(const std::string &symbol, double price);
    //void reloadCache(AssetValue* assetValue);
};


#endif //NF_TX_CORE_PRICECACHE_H
