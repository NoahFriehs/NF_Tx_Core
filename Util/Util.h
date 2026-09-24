
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

//! Utility function to split a string by a delimiter
std::vector<std::string> splitString(const std::string &input, char delimiter);

//! Split a CSV line with RFC-4180 quote handling: a quoted field keeps
//! embedded delimiters, doubled quotes inside a quoted field are an
//! escaped quote, and quotes not at the start of a field are literals.
//! For lines without quotes the result is identical to splitString.
std::vector<std::string> splitCsvLine(const std::string &input, char delimiter);

class TimestampConverter {
public:
    //! Convert a string to a tm struct
    static std::tm stringToTm(const std::string &timestamp_str);

    //! Convert a tm struct to a string
    static std::string tmToString(const std::tm &timestamp_tm);
};

//! Utility function to remove a prefix from a string
std::string removePrefix(std::string string, const std::string &prefixToRemove);

std::string removeAllOccurrences(std::string str, char charToRemove);

//! Utility function to return thr Kraken currencyType
std::string getKrakenCurrencyType(const std::string &currencyType);

#endif //NF_TX_CORE_UTIL_H
