//
// Created by nfriehs on 11/19/23.
//

#ifndef NF_TX_CORE_STATICPRICES_H
#define NF_TX_CORE_STATICPRICES_H

#include <unordered_map>
#include <string>

class StaticPrices {
public:
    std::unordered_map<std::string, double> prices;

    StaticPrices() {
        prices["DOGE"] = 0.07;
        prices["CUDOS"] = 0.002;
        prices["ETH"] = 1300;
        prices["BTC"] = 16700.0;
        prices["CRO"] = 0.06;
        prices["EUR"] = 1.0;
        prices["ETHW"] = 0.0;
        prices["LUNA2"] = 1.46;
        prices["LUNC"] = 0.0002;
        prices["LUNA"] = 0.0;
        prices["ALGO"] = 0.20;
    }
};

#endif //NF_TX_CORE_STATICPRICES_H
