#include "library.h"
#include "FileLog.h"
#include "DataHolder.h"
#include "TransactionParser.h"
#include "TransactionManager.h"
#include "TimeSpan.h"

#include <iostream>

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
    parser.parseFromCsv(TransactionParser::Mode::CDC);

    long double time = timeSpan.end();
    FileLog::i("library", "Parsing took " + std::to_string(time) + " milliseconds");

    auto *transactionManager = new TransactionManager(parser.getTransactions());

    timeSpan.start();

    transactionManager->processTransactions();

    time = timeSpan.end();
    FileLog::i("library", "Processing took " + std::to_string(time) + " milliseconds");

    dataHolder.SetTransactionManager(transactionManager);

    // return true if successful
    return dataHolder.isInitialized();
}

#ifdef __cplusplus
}
#endif
