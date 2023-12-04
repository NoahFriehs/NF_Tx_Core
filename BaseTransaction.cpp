//
// Created by nfriehs on 11/7/23.
//

#include "BaseTransaction.h"
#include "Util.h"

int txIdCounter;

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


long double BaseTransaction::getAmount() {
    return amount;
}

long double BaseTransaction::getNativeAmount() {
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

int BaseTransaction::getWalletId() {
    return walletId;
}

long double BaseTransaction::getAmountBonus() {
    return amountBonus;
}

long double BaseTransaction::getToAmount() {
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
