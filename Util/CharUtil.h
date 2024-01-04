//
// Created by nfriehs on 12/15/23.
//

#ifndef NF_TX_CORE_CHARUTIL_H
#define NF_TX_CORE_CHARUTIL_H

#include "../FileLog.h"
#include "../MagicNumbers.h"
#include <cstring>

// Utility function to copy a 2D char array
void copyCharArray(char **destination, char **source, int rows, int cols);

//! Utility function to copy a string to a char array
void stringToCharArray(char destination[MAX_STRING_LENGTH], const std::string &src);

//! Utility function to copy a char array to a string
std::string charArrayToString(char source[]);

//! Utility function to copy a char array to a string
void copyCharArrayToString(std::string &destination, const char source[MAX_STRING_LENGTH]);

#endif //NF_TX_CORE_CHARUTIL_H
