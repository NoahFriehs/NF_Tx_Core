
#ifndef NF_TX_CORE_CHARUTIL_H
#define NF_TX_CORE_CHARUTIL_H

#include "../FileLog.h"
#include <cstring>
#include <cctype>
#include <cstdio>

//! Copy a string to a char array. The destination is always NUL-terminated
//! and the copy is bounded by `size` bytes.
void stringToCharArray(char *destination, size_t size, const std::string &src);

//! Copy a char array to a string
std::string charArrayToString(char source[]);

//! Copy a char array to a string
void copyCharArrayToString(std::string &destination, const char source[]);

#endif //NF_TX_CORE_CHARUTIL_H
