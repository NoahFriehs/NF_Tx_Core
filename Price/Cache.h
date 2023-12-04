//
// Created by nfriehs on 11/19/23.
//

#ifndef NF_TX_CORE_CACHE_H
#define NF_TX_CORE_CACHE_H

#include <string>
#include <chrono>

// Cache class to store a symbol with the price and the time of creation
class Cache {
private:
    std::string id;
    double price;
    std::chrono::time_point<std::chrono::system_clock> creationTime;

public:
    Cache(std::string id, double price);

    bool isOlderThanFiveMinutes() const;

    double getPrice() const;
};


#endif //NF_TX_CORE_CACHE_H
