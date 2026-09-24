
#include "CharUtil.h"

// Utility function to copy a 2D char array
void copyCharArray(char **destination, char **source, int rows, int cols) {
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            destination[i][j] = source[i][j];
        }
    }
}

void stringToCharArray(char *destination, size_t size, const std::string &src) {
    if (destination == nullptr || size == 0) return;
    if (src.size() >= size) {
        FileLog::w("CharUtil",
                   "String truncated from " + std::to_string(src.size()) + " to " +
                   std::to_string(size - 1) + " characters");
    }
    // snprintf is bounded by size and always NUL-terminates.
    std::snprintf(destination, size, "%s", src.c_str());
}

std::string charArrayToString(char source[]) {
    return std::string(source);
}

void copyCharArrayToString(std::string &destination, const char source[]) {
    destination = source;
}
