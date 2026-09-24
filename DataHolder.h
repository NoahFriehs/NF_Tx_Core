
#ifndef NF_TX_CORE_DATAHOLDER_H
#define NF_TX_CORE_DATAHOLDER_H


#include <mutex>
#include <stdexcept>
#include <utility>
#include <memory>
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

    // Destructor to properly clean up resources
    ~DataHolder() {
        std::lock_guard<std::mutex> lock(mutexData);
        transactionManager.reset();
    }

    //! Set the TransactionManager (the previous instance is freed automatically)
    void SetTransactionManager(std::unique_ptr<TransactionManager> tm) {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!tm) throw std::invalid_argument("Null pointer to TransactionManager");
        transactionManager = std::move(tm);
        initialized_ = transactionManager->isReady();
    }

    //! Get the TransactionManager (pointer owned by the DataHolder)
    TransactionManager *GetTransactionManager() {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        transactionManager->checkTransactionManagerState();
        return transactionManager.get();
    }


    //! Check if the TransactionManager is initialized
    bool isInitialized() {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        return initialized_;
    }

    //! Save the data to a directory
    void saveData(const std::string &dirPath) {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        transactionManager->saveData(dirPath);
    }

    //! Load the data from a directory
    void loadData(const std::string &dirPath) {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        transactionManager->loadData(dirPath);
    }

    //! Check if the data is saved
    bool checkSavedData() {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        return transactionManager->checkSavedData();
    }

private:

    bool initialized_ = false;
    mutable std::mutex mutexData; // mutable allows locking in const functions

    std::unique_ptr<TransactionManager> transactionManager;

    DataHolder() = default;

};


#endif //NF_TX_CORE_DATAHOLDER_H
