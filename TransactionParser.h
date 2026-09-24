
#ifndef NF_TX_CORE_TRANSACTIONPARSER_H
#define NF_TX_CORE_TRANSACTIONPARSER_H


#include <string>
#include "TransactionManager.h"
#include "Transaction/BaseTransaction.h"

class TransactionParser {

public:
    TransactionParser();

    explicit TransactionParser(const std::vector<std::string> &data);

    ~TransactionParser();

    //! Parse the data from the csv with the given mode
    void parseFromCsv(Mode mode);

    //! Return the transactions
    std::vector<BaseTransaction> &getTransactions();

    //! Number of lines that could not be parsed (skipped, like the
    //! Kotlin core's amountTxFailed)
    size_t getFailedLines() const { return failedLines; }

private:
    std::vector<std::string> data;
    std::vector<BaseTransaction> transactions;
    bool hasData = false;
    size_t failedLines = 0;

    void parseCDC();

    void parseCard();

    void parseKraken();
};


#endif //NF_TX_CORE_TRANSACTIONPARSER_H
