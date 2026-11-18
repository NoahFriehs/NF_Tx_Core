#include "DataHolder.h"

#include <string>

#include "ExportCsv.h"
#include "FileLog.h"

// The save/load funnels carry the sheet section with the store: the sheet
// state injected from the holder (the single source of truth) right before
// the write, read back into the holder after the read.
bool DataHolder::saveData(const std::string &dirPath) {
    std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
    if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
    transactionManager->setBalanceSheetText(sheet_.serialize(SheetSerde::todayWallClock()));
    return transactionManager->saveData(dirPath);
}

void DataHolder::loadData(const std::string &dirPath) {
    std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
    if (!transactionManager) throw std::runtime_error("TransactionManager not initialized");
    transactionManager->loadData(dirPath);
    const std::string text = transactionManager->getBalanceSheetText();
    // A pre-v5 store carries the empty section (the in-memory upgrade); a
    // v5 store carries the written sheet. A corrupted section degrades
    // line- by line- (rejected lines are skipped and counted, the good
    // lines load).
    const size_t rejected = sheet_.parse(text);
    if (rejected > 0) {
        FileLog::e("DataHolder",
                   "Balance-sheet section load: " + std::to_string(rejected) +
                           " line(s) rejected (skipped)");
    }
}

void DataHolder::sheetAutoSave() const {
    const std::string dir = transactionManager->getSaveDir();
    if (dir.empty()) {
        // No store location known yet (nothing was ever loaded or saved
        // on this core incarnation): the entry lives in memory and the
        // app's next regular save (it runs after every import/restore)
        // persists it.
        FileLog::w("DataHolder",
                   "Sheet mutation without a save dir: kept in memory, the next save persists it");
        return;
    }
    transactionManager->setBalanceSheetText(sheet_.serialize(SheetSerde::todayWallClock()));
    if (!transactionManager->saveData(dir)) {
        // Degrade only the persistence: the entry is in memory, the next
        // app save retries (same stance as the import-save path).
        FileLog::e("DataHolder", "Sheet auto-save failed (the entry is in memory)");
    }
}

bool DataHolder::sheetSet(const std::string &line) {
    std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
    if (!transactionManager) {
        FileLog::e("DataHolder", "sheetSet before init: no live core");
        return false;
    }
    if (!sheet_.setLine(line)) {
        FileLog::e("DataHolder", "sheetSet: the core validator rejected the line");
        return false;
    }
    sheetAutoSave();
    return true;
}

bool DataHolder::sheetDelete(const std::string &kindId) {
    std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
    if (!transactionManager) {
        FileLog::e("DataHolder", "sheetDelete before init: no live core");
        return false;
    }
    if (!sheet_.deleteEntry(kindId)) {
        FileLog::e("DataHolder", "sheetDelete: unknown sheet entry " + kindId);
        return false;
    }
    sheetAutoSave();
    return true;
}

std::vector<std::string> DataHolder::sheetList(const std::string &kind) {
    std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
    if (!transactionManager) return {};
    return sheet_.listLines(kind, SheetSerde::todayWallClock());
}

std::vector<std::string> DataHolder::sheetSnapshot() {
    std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
    if (!transactionManager) return {};
    return sheet_.snapshot(
            transactionManager->getTotalValueOfAssetsIncludingBuffer(),
            SheetSerde::todayWallClock());
}

std::string DataHolder::exportCsv() {
    std::lock_guard<std::mutex> lock(mutexData); // Thread-safe access
    if (!transactionManager) return {};
    // One instant for the whole file: the liability balances and the
    // trailing stamp share the same wall clock, so the archive is
    // self-consistent.
    return CsvExport::buildCsvFile(*transactionManager, sheet_,
                                   SheetSerde::todayWallClock());
}
