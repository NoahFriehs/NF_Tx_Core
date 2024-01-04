//
// Created by nfriehs on 11/19/23.
//

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

    //! Get the price of a symbol
    double getPrice(const std::string &symbol);

    //! Load the cache with data
    void
    loadCacheWithData(const std::vector<std::string> &symbols, const std::vector<double> &prices);
};


#endif //NF_TX_CORE_ASSETVALUE_H
