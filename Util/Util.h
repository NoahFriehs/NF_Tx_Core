//
// Created by nfriehs on 10/31/23.
//

#ifndef NF_TX_CORE_UTIL_H
#define NF_TX_CORE_UTIL_H

#include <string>
#include <iostream>
#include <sstream>
#include <ctime>
#include <vector>
#include "../Enums.h"


// Function to convert a string to TransactionType
TransactionType ttConverter(const std::string &s);

std::vector<std::string> splitString(const std::string &input, char delimiter);

class TimestampConverter {
public:
    static std::tm stringToTm(const std::string &timestamp_str);

    static std::string tmToString(const std::tm &timestamp_tm);
};

std::string removePrefix(std::string string, const std::string &prefixToRemove);

#endif //NF_TX_CORE_UTIL_H
