
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
    //! Constructor (timestamps at creation)
    Cache(std::string id, double price);

    //! Constructor with an explicit creation time (test seam for aging an
    //! entry past the TTL without sleeping - mirrors the Kotlin Cache
    //! test constructor).
    Cache(std::string id, double price,
          std::chrono::time_point<std::chrono::system_clock> creationTime);

    //! Check if the cache is valid
    bool isOlderThanFiveMinutes() const;

    //! Get the price
    double getPrice() const;
};


#endif //NF_TX_CORE_CACHE_H
