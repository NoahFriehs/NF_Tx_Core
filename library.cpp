#include "library.h"
#include "FileLog.h"
#include "DataHolder.h"
#include "TransactionParser.h"
#include "TransactionManager.h"
#include "TimeSpan.h"

#include <iostream>
#include <utility>

#ifdef __cplusplus
extern "C" {
#endif

void hello() {
    std::cout << "Hello, World! 2x3c" << std::endl;
}



bool init() {
    auto txManager = DataHolder::GetInstance().GetTransactionManager();
    return txManager->isReady();
}

bool initWithData(const std::vector<std::string>& data, uint mode) {

    FileLog::init("my_log.txt", true, 3 );

    FileLog::i("library", "Initializing with data...");
    FileLog::i("library", "Data size: " + std::to_string(data.size()));

    // init DataHolder
    DataHolder &dataHolder = DataHolder::GetInstance();

    TimeSpan timeSpan;
    timeSpan.start();

    // parseFromCsv data
    TransactionParser parser(data);
    try {
        parser.parseFromCsv(static_cast<TransactionParser::Mode>(mode));
    } catch (std::exception &e) {   // aka Data sanitization
        FileLog::e("library", e.what());
        return false;
    }

    long double time = timeSpan.end();
    FileLog::i("library", "Parsing took " + std::to_string(time) + " milliseconds");

    auto *transactionManager = new TransactionManager(parser.getTransactions());

    timeSpan.start();

    transactionManager->processTransactions();

    time = timeSpan.end();
    FileLog::i("library", "Processing took " + std::to_string(time) + " milliseconds");

    dataHolder.SetTransactionManager(transactionManager);

    timeSpan.start();
    transactionManager->calculateWalletBalances();

    time = timeSpan.end();
    FileLog::i("library", "Calculating balances took " + std::to_string(time) + " milliseconds");

    // return true if successful
    return dataHolder.isInitialized();
}


std::vector<std::string> getCurrencies() {
    return DataHolder::GetInstance().GetTransactionManager()->getCurrencies();
}

void setPrice(std::vector<double> prices) {
    DataHolder::GetInstance().GetTransactionManager()->setPrices(std::move(prices));
    DataHolder::GetInstance().GetTransactionManager()->calculateWalletBalances();
}


#ifdef __cplusplus
}
#endif
