//
// Created by nfriehs on 11/14/23.
//

#ifndef NF_TX_CORE_TIMESPAN_H
#define NF_TX_CORE_TIMESPAN_H

#include <chrono>

class TimeSpan {
private:
    std::chrono::time_point<std::chrono::steady_clock> startTime;

public:
    void start() {
        startTime = std::chrono::steady_clock::now();
    }

    long double end() {
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
        return duration.count() / 1'000'000.0; // Convert nanoseconds to milliseconds
    }
};

#endif //NF_TX_CORE_TIMESPAN_H
