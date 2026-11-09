
#include "Cache.h"

#include <utility>

Cache::Cache(std::string id, double price) : id(std::move(id)), price(price) {
    creationTime = std::chrono::system_clock::now();
}

Cache::Cache(std::string id, double price,
             std::chrono::time_point<std::chrono::system_clock> creationTime)
        : id(std::move(id)), price(price), creationTime(creationTime) {}

bool Cache::isOlderThanFiveMinutes() const {
    auto currentTime = std::chrono::system_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - creationTime).count();
    return elapsedTime > 300;
}

double Cache::getPrice() const {
    return price;
}