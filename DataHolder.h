//
// Created by nfriehs on 11/7/23.
//

#ifndef NF_TX_CORE_DATAHOLDER_H
#define NF_TX_CORE_DATAHOLDER_H


#include <mutex>
#include <stdexcept>
#include <utility>
#include "TransactionManager.h"

class DataHolder {

public:
    static DataHolder &GetInstance() {
        static DataHolder instance;
        return instance;
    }

    // Delete copy constructor and assignment operation to ensure uniqueness
    DataHolder(DataHolder const &) = delete;

    DataHolder &operator=(DataHolder const &) = delete;


    void SetTransactionManager(TransactionManager *tm) {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!tm) throw std::invalid_argument("Null pointer to TransactionManager");
        transactionManager = tm;
        initialized_ = transactionManager->isReady();
    }

    TransactionManager *GetTransactionManager() {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        transactionManager->checkTransactionManagerState();
        return transactionManager;
    }


    bool isInitialized() {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        return initialized_;
    }

    void saveData(const std::string &filePath) {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        transactionManager->saveData(filePath);
    }

    void loadData(const std::string &filePath) {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        return transactionManager->loadData(filePath);
    }

    bool checkSavedData() {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        return transactionManager->checkSavedData();
    }

private:

    bool initialized_ = false;
    mutable std::mutex mutexData; // mutable allows locking in const functions

    TransactionManager *transactionManager = nullptr;

    DataHolder() = default;

};


#endif //NF_TX_CORE_DATAHOLDER_H
