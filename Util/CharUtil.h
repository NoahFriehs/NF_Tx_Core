//
// Created by nfriehs on 11/11/23.
//

#ifndef NF_TX_CORE_CHARUTIL_H
#define NF_TX_CORE_CHARUTIL_H

#include "../FileLog.h"
#include "../MagicNumbers.h"
#include <cstring>
#include <cctype>
#include <cstdio>

// Utility function to copy a 2D char array
void copyCharArray(char **destination, char **source, int rows, int cols);

//! Copy a string to a char array.
//! The destination is always NUL-terminated and the copy is bounded by
//! `size` bytes, so it can safely be used with arrays of any size
//! (the old overload claimed a MAX_STRING_LENGTH capacity, which overflowed
//! smaller buffers such as CTransactionStruct.transactionTypeString[20]).
void stringToCharArray(char *destination, size_t size, const std::string &src);

//! Copy a char array to a string
std::string charArrayToString(char source[]);

//! Copy a char array to a string
void copyCharArrayToString(std::string &destination, const char source[]);

#endif //NF_TX_CORE_CHARUTIL_H
