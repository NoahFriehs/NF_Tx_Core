//
// Created by nfriehs on 10/31/23.
//

#include "Wallet.h"

#include <utility>

int walletIdCounter = 0;

Wallet::Wallet() {
    walletId = walletIdCounter++;
}

Wallet::~Wallet() = default;

Wallet::Wallet(std::string currencyType) : Wallet() {
    this->currencyType = std::move(currencyType);
}

void Wallet::setIsOutWallet(bool b) {
    isOutsideWallet = b;
}

bool Wallet::addTransaction(BaseTransaction &transaction) {
        transactions.push_back(transaction);
        balance += transaction.getAmount();
        nativeBalance += transaction.getNativeAmount();
        return true;
}

int Wallet::getWalletId() {
    return walletId;
}

bool Wallet::withdraw(BaseTransaction &transaction) {
        transactions.push_back(transaction);
        balance -= transaction.getAmount();
        nativeBalance -= transaction.getNativeAmount();
        return true;
}

std::vector<BaseTransaction> Wallet::getTransactions() {
    return transactions;
}
