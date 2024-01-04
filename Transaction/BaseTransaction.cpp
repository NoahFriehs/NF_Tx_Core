//
// Created by nfriehs on 11/7/23.
//

#include "BaseTransaction.h"
#include "../Util/Util.h"

int txIdCounter;    //TODO: be careful with this when loading from DB

BaseTransaction::BaseTransaction() = default;

BaseTransaction::~BaseTransaction() = default;

void BaseTransaction::parseCDC(const std::string &txString) {
    auto tx = splitString(txString, ',');

    transactionId = txIdCounter++;
    transactionDate = TimestampConverter::stringToTm(tx[0]);
    description = tx[1];
    currencyType = tx[2];
    amount = std::stold(tx[3]);
    nativeAmount = std::stold(tx[7]);
    transactionTypeString = tx[9];
    transactionType = ttConverter(transactionTypeString);

    if (tx.size() == 11) transactionHash = tx[10];
    if (transactionType == viban_purchase) {
        toCurrencyType = tx[4];
        toAmount = std::stold(tx[5]);
    }

}

std::string BaseTransaction::getCurrencyType() {
    return currencyType;
}


long double BaseTransaction::getAmount() const {
    return amount;
}

long double BaseTransaction::getNativeAmount() const {
    return nativeAmount;
}

TransactionType BaseTransaction::getTransactionType() {
    return transactionType;
}

void BaseTransaction::setAmountToAmountBonus() {
    amountBonus = amount;
}

void BaseTransaction::setWalletId(int id) {
    walletId = id;
}

void BaseTransaction::setFromWalletId(int id) {
    fromWalletId = id;
}

std::string BaseTransaction::getToCurrencyType() {
    return toCurrencyType;
}

std::string BaseTransaction::getTransactionTypeString() {
    return transactionTypeString;
}

int BaseTransaction::getWalletId() const {
    return walletId;
}

long double BaseTransaction::getAmountBonus() const {
    return amountBonus;
}

long double BaseTransaction::getToAmount() const {
    return toAmount;
}

TransactionData BaseTransaction::getTransactionData() {
    TransactionData txData;
    txData.transactionId = transactionId;
    txData.walletId = walletId;
    txData.fromWalletId = fromWalletId;
    txData.description = description;
    txData.transactionDate = transactionDate;
    txData.currencyType = currencyType;
    txData.toCurrencyType = toCurrencyType;
    txData.amount = amount;
    txData.toAmount = toAmount;
    txData.nativeAmount = nativeAmount;
    txData.amountBonus = amountBonus;
    txData.transactionTypeOrdinal = transactionType;
    txData.transactionHash = transactionHash;
    txData.isOutsideTransaction = isOutsideTransaction;
    txData.notes = notes;

    return txData;
}

void BaseTransaction::parseCard(const std::string &txString) {
    auto tx = splitString(txString, ',');

    transactionId = txIdCounter++;
    transactionDate = TimestampConverter::stringToTm(tx[0]);
    description = tx[1];
    currencyType = tx[2];
    amount = std::stold(tx[3]);
    nativeAmount = std::stold(tx[7]);
    transactionTypeString = tx[1];
    transactionType = STRING;

}

TransactionStruct BaseTransaction::getTransactionStruct() {
    TransactionStruct data;
    data.transactionId = transactionId;
    data.walletId = walletId;
    data.fromWalletId = fromWalletId;
    data.description = description;
    data.transactionDate = transactionDate;
    data.currencyType = currencyType;
    data.toCurrencyType = toCurrencyType;
    data.amount = amount;
    data.toAmount = toAmount;
    data.nativeAmount = nativeAmount;
    data.amountBonus = amountBonus;
    data.transactionType = transactionType;
    data.transactionTypeString = transactionTypeString;
    data.transactionHash = transactionHash;
    data.isOutsideTransaction = isOutsideTransaction;
    data.notes = notes;
    return data;
}

void BaseTransaction::fromTransactionStruct(const TransactionStruct &data) {
    transactionId = data.transactionId;
    walletId = data.walletId;
    fromWalletId = data.fromWalletId;
    description = data.description;
    transactionDate = data.transactionDate;
    currencyType = data.currencyType;
    toCurrencyType = data.toCurrencyType;
    amount = data.amount;
    toAmount = data.toAmount;
    nativeAmount = data.nativeAmount;
    amountBonus = data.amountBonus;
    transactionType = data.transactionType;
    transactionTypeString = data.transactionTypeString;
    transactionHash = data.transactionHash;
    isOutsideTransaction = data.isOutsideTransaction;
    notes = data.notes;
}

void BaseTransaction::setTxIdCounter(int txIdCounter_) {
    if (txIdCounter_ <= txIdCounter)
        txIdCounter = txIdCounter_;
    else {
        FileLog::e("BaseTransaction", "trying to set a invalid txIdCounter");
        throw std::runtime_error("trying to set a invalid txIdCounter");
    }
}

int BaseTransaction::getTxIdCounter() {
    return txIdCounter;
}

int BaseTransaction::getTransactionId() const {
    return transactionId;
}

void BaseTransaction::setTransactionData(const TransactionStruct &txStruct) {
    transactionId = txStruct.transactionId;
    walletId = txStruct.walletId;
    fromWalletId = txStruct.fromWalletId;
    description = txStruct.description;
    transactionDate = txStruct.transactionDate;
    currencyType = txStruct.currencyType;
    toCurrencyType = txStruct.toCurrencyType;
    amount = txStruct.amount;
    toAmount = txStruct.toAmount;
    nativeAmount = txStruct.nativeAmount;
    amountBonus = txStruct.amountBonus;
    transactionType = txStruct.transactionType;
    transactionTypeString = txStruct.transactionTypeString;
    transactionHash = txStruct.transactionHash;
    isOutsideTransaction = txStruct.isOutsideTransaction;
    notes = txStruct.notes;
}

void BaseTransaction::setTransactionTypeString(const std::string &transactionTypeStringToSet) {
    //BaseTransaction::transactionTypeString = transactionTypeStringToSet;
}
