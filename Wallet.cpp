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
    this->currencyType = currencyType;
}

void Wallet::setIsOutWallet(bool b) {
    isOutsideWallet = b;
}

bool Wallet::addTransaction(BaseTransaction &transaction) {
    transactions.push_back(transaction);
    balance += transaction.getAmount();
    moneySpent += transaction.getNativeAmount();
    bonusBalance += transaction.getAmountBonus();
    return true;
}

int Wallet::getWalletId() {
    return walletId;
}

bool Wallet::withdraw(BaseTransaction &transaction) {
    transactions.push_back(transaction);
    balance -= transaction.getAmount();
    moneySpent -= transaction.getNativeAmount();
    return true;
}

std::vector<BaseTransaction> Wallet::getTransactions() {
    return transactions;
}

long double Wallet::getNativeBalance() const {
    return nativeBalance;
}

long double Wallet::getBonusBalance() const {
    return bonusBalance;
}

long double Wallet::getMoneySpent() const {
    return moneySpent;
}

std::string Wallet::getCurrencyType() const {
    return currencyType;
}

long double Wallet::getBalance() const {
    return balance;
}

void Wallet::addToTransaction(BaseTransaction &transaction) {
    transactions.push_back(transaction);
    balance += transaction.getToAmount();
    moneySpent += transaction.getNativeAmount();
    bonusBalance += transaction.getAmountBonus();
}

WalletData Wallet::getWalletData() {
    WalletData walletData;
    walletData.walletId = walletId;
    walletData.currencyType = currencyType;
    walletData.balance = balance;
    walletData.nativeBalance = nativeBalance;
    walletData.bonusBalance = bonusBalance;
    walletData.moneySpent = moneySpent;
    walletData.isOutsideWallet = isOutsideWallet;
    walletData.notes = notes;
    return walletData;
}
