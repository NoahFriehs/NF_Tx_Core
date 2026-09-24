// Native binary persistence helpers.
//
// NOTE: this format is a plain memory dump of POD structs. It is NOT
// portable between architectures/ABIs (padding, long double layout) and is
// intended as a speed feature only. To make format problems detectable
// instead of silent, every file written here starts with a FileHeader:
//
//     char  magic[4]   "CWCP"
//     int   version    2
//     int   longDoubleSize  sizeof(long double) on the writing machine
//
// Files without a matching header are treated as empty/corrupt rather than
// read into live structs.

#ifndef NF_TX_CORE_BINARYUTIL_H
#define NF_TX_CORE_BINARYUTIL_H


#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "FileLog.h"
#include "MagicNumbers.h"

namespace BinaryUtil {

// Header for every serialized file. See the comment at the top of this file.
struct FileHeader {
    char magic[4]{'C', 'W', 'C', 'P'};
    int version{2};
    int longDoubleSize{static_cast<int>(sizeof(long double))};
};

bool writeHeader(std::ofstream &file) {
    FileHeader header;
    file.write(reinterpret_cast<const char *>(&header), sizeof(FileHeader));
    return file.good();
}

// Returns true if a header was read and matches this machine.
bool readAndValidateHeader(std::ifstream &file) {
    FileHeader header;
    file.read(reinterpret_cast<char *>(&header), sizeof(FileHeader));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(FileHeader))) {
        FileLog::w("BinaryUtil", "File too small for header, treating as empty");
        return false;
    }
    if (std::memcmp(header.magic, "CWCP", 4) != 0) {
        FileLog::w("BinaryUtil", "Bad magic, treating file as corrupt");
        return false;
    }
    if (header.version != 2) {
        FileLog::w("BinaryUtil", "Unsupported version " + std::to_string(header.version));
        return false;
    }
    if (header.longDoubleSize != static_cast<int>(sizeof(long double))) {
        FileLog::e("BinaryUtil",
                   "long double size mismatch (" + std::to_string(header.longDoubleSize) +
                           " vs " + std::to_string(sizeof(long double)) +
                           "), not portable - ignoring file content");
        return false;
    }
    return true;
}

// Serialize single struct to binary file
template<typename T>
void serializeStruct(const T &data, const std::string &fileName) {
    std::ofstream file(fileName, std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        if (writeHeader(file) &&
            file.write(reinterpret_cast<const char *>(&data), sizeof(T))) {
            FileLog::d("BinaryUtil", "Serialized struct to " + fileName);
        } else {
            FileLog::e("BinaryUtil", "Struct serialization failed: " + fileName);
        }
    } else {
        FileLog::e("BinaryUtil", "Error opening file for serialization: " + fileName);
    }
}

// Deserialize single struct from binary file. On any problem the data is
// zero-initialized instead of being left half-read.
template<typename T>
void deserializeStruct(T &data, const std::string &fileName) {
    std::ifstream file(fileName, std::ios::binary);

    if (!file.is_open() || !readAndValidateHeader(file)) {
        std::memset(&data, 0, sizeof(T));
        FileLog::i("BinaryUtil", "No valid struct in " + fileName + ", using empty state");
        return;
    }

    if (file.read(reinterpret_cast<char *>(&data), sizeof(T)) && file.gcount() == sizeof(T)) {
        FileLog::d("BinaryUtil", "Deserialized struct from " + fileName);
    } else {
        uint16_t readCount = 0;
        file.read(reinterpret_cast<char *>(&readCount), sizeof(uint16_t));
        std::memset(&data, 0, sizeof(T));
        FileLog::e("BinaryUtil",
                   "Wrong file size " + std::to_string(readCount) + " in " + fileName +
                           ", using empty state");
    }
}

// Serialize vector of structs to binary file
template<typename T>
void serializeVector(const std::vector<T> &data, const std::string &fileName) {
    std::ofstream file(fileName, std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        uint64_t count = data.size();
        bool ok = writeHeader(file);
        if (ok) file.write(reinterpret_cast<const char *>(&count), sizeof(uint64_t));
        for (const auto &item: data) {
            if (!ok) break;
            if (!file.write(reinterpret_cast<const char *>(&item), sizeof(T))) {
                ok = false;
            }
        }
        if (ok) {
            FileLog::d("BinaryUtil", "Serialized " + std::to_string(data.size()) +
                                               " items to " + fileName);
        } else {
            FileLog::e("BinaryUtil", "Vector serialization failed: " + fileName);
        }
    } else {
        FileLog::e("BinaryUtil", "Error opening file for vector serialization: " + fileName);
    }
}

// Read a vector size field and clamp it to a sane range. A corrupted or
// malicious count must not cause gigabytes of allocation.
uint64_t readClampedCount(std::ifstream &file, const std::string &fileName) {
    uint64_t count = 0;
    if (!file.read(reinterpret_cast<char *>(&count), sizeof(uint64_t))) {
        return 0;
    }
    // No realistic use creates more than 10 000 wallets/transactions.
    const uint64_t MAX_COUNT = 10000;
    if (count > MAX_COUNT) {
        FileLog::e("BinaryUtil",
                   "Implausible item count " + std::to_string(count) + " in " + fileName +
                           ", clamping to " + std::to_string(MAX_COUNT));
        return MAX_COUNT;
    }
    return count;
}

// Deserialize vector of structs from binary file. Missing/invalid files yield
// an empty vector. For CWalletStruct, the per-item transaction count is
// clamped to MAX_TRANSACTIONS after each item is read, so a corrupted
// numTransactions can never address memory beyond the fixed array.
template<typename T>
void deserializeVector(std::vector<T> &data, const std::string &fileName) {
    data.clear();
    std::ifstream file(fileName, std::ios::binary);

    if (!file.is_open() || !readAndValidateHeader(file)) {
        FileLog::i("BinaryUtil", "No valid vector in " + fileName + ", using empty data");
        return;
    }

    auto count = readClampedCount(file, fileName);
    data.reserve(static_cast<size_t>(count));
    size_t readItems = 0;
    while (readItems < count) {
        T item;
        std::memset(&item, 0, sizeof(T));
        if (file.read(reinterpret_cast<char *>(&item), sizeof(T)) &&
            file.gcount() == sizeof(T)) {
            constexpr bool isCWalletStruct = std::is_same<T, CWalletStruct>::value;
            if constexpr (isCWalletStruct) {
                if (item.numTransactions > MAX_TRANSACTIONS) {
                    FileLog::e("BinaryUtil",
                               "Corrupt numTransactions " + std::to_string(item.numTransactions) +
                                       " in " + fileName + ", clamping to " +
                                       std::to_string(MAX_TRANSACTIONS));
                    item.numTransactions = MAX_TRANSACTIONS;
                }
            }
            data.push_back(std::move(item));
        } else {
            FileLog::e("BinaryUtil",
                       "Truncated vector in " + fileName + ", read " +
                               std::to_string(readItems) + "/" + std::to_string(count));
            break;
        }
        readItems++;
    }
    FileLog::d("BinaryUtil", "Deserialized " + std::to_string(data.size()) + " items from " +
                                           fileName);
}

} // namespace BinaryUtil

#endif //NF_TX_CORE_BINARYUTIL_H
