//
// Created by nfriehs on 12/15/23.
//

#include "CharUtil.h"

// Utility function to copy a 2D char array
void copyCharArray(char** destination, char** source, int rows, int cols) {
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            destination[i][j] = source[i][j];
        }
    }
}

void stringToCharArray(char destination[MAX_STRING_LENGTH], const std::string& src) {
    if (src.size() <= MAX_STRING_LENGTH) {
        std::strcpy(destination, src.c_str());
    } else {
        FileLog::w("CharUtil", "String is too long to convert to char array");
        auto shortStr = src.substr(0, MAX_STRING_LENGTH - 1);
        std::strcpy(destination, shortStr.c_str());
    }
}

std::string charArrayToString(char source[]) {
    return std::string(source);
}

void copyCharArrayToString(std::string& destination, const char source[MAX_STRING_LENGTH]) {
    destination = source;
}