//
// Created by nfriehs on 11/7/23.
//

#include <vector>
#include <stdexcept>
#include "TransactionParser.h"
#include "FileLog.h"

TransactionParser::TransactionParser() = default;

TransactionParser::~TransactionParser() = default;

TransactionParser::TransactionParser(const std::vector<std::string> &data) {
    if (data.empty()) throw std::invalid_argument("Data is empty");
    this->data = data;
    hasData = true;
}

void TransactionParser::parseFromCsv(Mode mode) {

    if (!hasData) throw std::invalid_argument("Data is empty");

    switch (mode) {

        case CDC: {
            parseCDC();
            break;
        }
        case Card:
            parseCard();
            break;
        case Kraken:
            parseKraken();
            break;
        case Custom:
            throw std::invalid_argument("Custom mode not implemented");
        case Default:
            parseCDC();
            break;
    }

}

std::vector<BaseTransaction> &TransactionParser::getTransactions() {
    return transactions;
}

void TransactionParser::parseCDC() {

    if ("Timestamp (UTC),Transaction Description,Currency,Amount,To Currency,To Amount,Native Currency,Native Amount,Native Amount (in USD),Transaction Kind,Transaction Hash" ==
        data[0])
        data.erase(data.begin());   // remove header row, if needed

    for (const auto &item: data) {
        BaseTransaction transaction;
        transaction.parseCDC(item);
        transactions.push_back(transaction);
    }

    FileLog::i("TransactionParser",
               "Parsed " + std::to_string(transactions.size()) + " transactions");

    data.clear();
    hasData = false;
}

void TransactionParser::parseCard() {

    if ("Timestamp (UTC),Transaction Description,Currency,Amount,To Currency,To Amount,Native Currency,Native Amount,Native Amount (in USD),Transaction Kind,Transaction Hash" ==
        data[0])
        data.erase(data.begin());   // remove header row, if needed

    for (const auto &item: data) {
        BaseTransaction transaction;
        transaction.parseCard(item);
        transactions.push_back(transaction);
    }

    FileLog::i("TransactionParser",
               "Parsed " + std::to_string(transactions.size()) + " transactions");

    data.clear();
    hasData = false;

}

void TransactionParser::parseKraken() {

    if (R"("txid","ordertxid","pair","time","type","ordertype","price","cost","fee","vol","margin","misc","ledgers")" ==
        data[0])
        data.erase(data.begin());   // remove header row, if needed

    for (const auto &item: data) {
        BaseTransaction transaction;
        transaction.parseKraken(item);
        transactions.push_back(transaction);
    }

    FileLog::i("TransactionParser",
               "Parsed " + std::to_string(transactions.size()) + " transactions");

    data.clear();
    hasData = false;
}
