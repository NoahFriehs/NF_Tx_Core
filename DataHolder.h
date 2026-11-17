
#ifndef NF_TX_CORE_DATAHOLDER_H
#define NF_TX_CORE_DATAHOLDER_H


#include <mutex>
#include <stdexcept>
#include <utility>
#include <memory>
#include "BalanceSheet.h"
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

    //! Save the data to a directory (created if it does not exist yet).
    //! Returns the result of the underlying TransactionManager::saveData.
    //! The user-owned sheet section is injected from the holder before the
    //! write (see the sheet block below).
    bool saveData(const std::string &dirPath);

    //! Load the data from a directory; the sheet section of the store is
    //! read back into the holder (pre-v5 stores load an empty sheet).
    void loadData(const std::string &dirPath);

    //! Check if the data is saved in the given directory
    bool checkSavedData(const std::string &dirPath) {
        std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
        if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
        return transactionManager->checkSavedData(dirPath);
    }

    // -------------------------------------------------------------------
    // The user-owned balance sheet (liabilities; the serde section is
    // generic, the other collections join in later). The sheet lives HERE,
    // in the process-lifetime holder - NOT in the TransactionManager - so
    // every TM swap the app's init/restore sequence performs leaves it
    // untouched. It round-trips through the core's save as the additive
    // v5 section (the manager is the transport); a sheet mutation
    // triggers the core's own (atomic) save when a save dir is known.
    // -------------------------------------------------------------------

    //! Upsert one sheet entry from its serde line (validate + store +
    //! auto-save). false = the line was rejected (the sheet is untouched)
    //! or there is no live core. A failed auto-save does NOT fail the
    //! call (the entry is in memory; the next app save persists it).
    bool sheetSet(const std::string &line);

    //! Delete the sheet entry [kindId] (kind prefix + id, e.g. "L<uuid>").
    //! false when the entry is unknown or there is no live core.
    bool sheetDelete(const std::string &kindId);

    //! The current sheet entry lines of [kind] ("L"), the balance of each
    //! computed at today. Empty when no live core.
    std::vector<std::string> sheetList(const std::string &kind);

    //! The sheet state (read path of the save plumbing + the tests).
    const BalanceSheet &getSheet() {
        std::lock_guard<std::mutex> lock(mutexData);
        return sheet_;
    }

private:
    // The auto-save of a sheet mutation (the caller holds mutexData).
    void sheetAutoSave() const;

    bool initialized_ = false;
    mutable std::mutex mutexData; // mutable allows locking in const functions

    std::unique_ptr<TransactionManager> transactionManager;
    BalanceSheet sheet_;

    DataHolder() = default;

};


#endif //NF_TX_CORE_DATAHOLDER_H
