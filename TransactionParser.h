//
// Created by nfriehs on 11/7/23.
//

#ifndef NF_TX_CORE_TRANSACTIONPARSER_H
#define NF_TX_CORE_TRANSACTIONPARSER_H


#include <string>
#include "TransactionManager.h"
#include "BaseTransaction.h"

class TransactionParser {

public:
    TransactionParser();
    explicit TransactionParser(const std::vector<std::string>& data);
    ~TransactionParser();

    enum Mode {
        CDC,
        Card,
        Custom
    };

    void parseFromCsv(Mode mode);

    std::vector<BaseTransaction> &getTransactions();

private:
    std::vector<std::string> data;
    std::vector<BaseTransaction> transactions;
    bool hasData = false;

    void parseCDC();
};


#endif //NF_TX_CORE_TRANSACTIONPARSER_H
