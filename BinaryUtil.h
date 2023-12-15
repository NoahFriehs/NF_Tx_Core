//
// Created by nfriehs on 12/14/23.
//

#ifndef NF_TX_CORE_BINARYUTIL_H
#define NF_TX_CORE_BINARYUTIL_H

#include <iostream>
#include <fstream>
#include <vector>

// Serialize single struct to binary file
template <typename T>
void serializeStruct(const T& data, const std::string& fileName) {
    std::ofstream file(fileName, std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&data), sizeof(T));
        file.close();
        std::cout << "Serialization successful.\n";
    } else {
        std::cerr << "Error opening file for serialization.\n";
    }
}

// Deserialize single struct from binary file
template <typename T>
void deserializeStruct(T& data, const std::string& fileName) {
    std::ifstream file(fileName, std::ios::binary);

    // Check if the file exists
    if (!file.is_open()) {
        std::ofstream createFile(fileName, std::ios::binary);
        createFile.close();

        file.open(fileName, std::ios::binary);
    }

    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(&data), sizeof(T));
        file.close();
        std::cout << "Deserialization successful.\n";
    } else {
        std::cerr << "Error opening file for deserialization.\n";
    }
}

// Serialize vector of structs to binary file
template <typename T>
void serializeVector(const std::vector<T>& data, const std::string& fileName) {
    std::ofstream file(fileName, std::ios::binary);
    if (file.is_open()) {
        for (const auto& item : data) {
            file.write(reinterpret_cast<const char*>(&item), sizeof(T));
        }
        file.close();
        std::cout << "Vector serialization successful.\n";
    } else {
        std::cerr << "Error opening file for vector serialization.\n";
    }
}

// Deserialize vector of structs from binary file
template <typename T>
void deserializeVector(std::vector<T>& data, const std::string& fileName) {
    std::ifstream file(fileName, std::ios::binary);

    // Check if the file exists
    if (!file.is_open()) {
        std::ofstream createFile(fileName, std::ios::binary);
        createFile.close();

        file.open(fileName, std::ios::binary);
    }

    if (file.is_open()) {
        T item;
        while (file.read(reinterpret_cast<char*>(&item), sizeof(T))) {
            data.push_back(item);
        }
        file.close();
        std::cout << "Vector deserialization successful.\n";
    } else {
        std::cerr << "Error opening file for vector deserialization.\n";
    }
}

#endif //NF_TX_CORE_BINARYUTIL_H
