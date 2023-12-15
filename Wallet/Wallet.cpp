//
// Created by nfriehs on 10/31/23.
//

#include "Wallet.h"
#include "../FileLog.h"

#include <utility>
#include <memory>

int walletIdCounter = 0;

Wallet::Wallet() {
    walletId = walletIdCounter++;
}

Wallet::~Wallet() = default;

Wallet::Wallet(std::string currencyType) : Wallet() {
    FileLog::v("Wallet", "Creating wallet with currency type: " + currencyType);
    this->currencyType = std::move(currencyType);
}

void Wallet::setIsOutWallet(bool b) {
    isOutsideWallet = b;
}

bool Wallet::addTransaction(BaseTransaction &transaction) {
    transaction.setWalletId(walletId);
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

std::unique_ptr<WalletData> Wallet::getWalletData() {
    WalletData walletData = {};
    walletData.walletId = walletId;
    walletData.currencyType = currencyType;
    walletData.balance = balance;
    walletData.nativeBalance = nativeBalance;
    walletData.bonusBalance = bonusBalance;
    walletData.moneySpent = moneySpent;
    walletData.isOutsideWallet = isOutsideWallet;
    walletData.notes = notes;
    return std::make_unique<WalletData>(walletData);
}

void Wallet::setCurrencyType(std::string currencyType_) {
    currencyType = currencyType_;
}

WalletStruct *Wallet::getWalletStruct() {
    auto data = new WalletStruct();
    data->walletId = walletId;
    for (auto &transaction: transactions) {
        data->transactions.push_back(transaction.getTransactionStruct());
    }
    data->currencyType = currencyType;
    data->balance = balance;
    data->nativeBalance = nativeBalance;
    data->bonusBalance = bonusBalance;
    data->moneySpent = moneySpent;
    data->isOutsideWallet = isOutsideWallet;
    data->notes = notes;
    return data;
}

void Wallet::setWalletData(const WalletStruct &data) {
    walletId = data.walletId;
    transactions.clear();
    for (auto &transactionData: data.transactions) {
        BaseTransaction transaction;
        transaction.fromTransactionStruct(transactionData);
        transactions.push_back(transaction);
    }
    currencyType = data.currencyType;
    balance = data.balance;
    nativeBalance = data.nativeBalance;
    bonusBalance = data.bonusBalance;
    moneySpent = data.moneySpent;
    isOutsideWallet = data.isOutsideWallet;
    notes = data.notes;
}

bool Wallet::getIsOutWallet() {
    return isOutsideWallet;
}

void Wallet::updateTransaction(BaseTransaction &transaction) {
    for (auto &tx: transactions) {
        if (tx.getTransactionStruct().transactionId == transaction.getTransactionStruct().transactionId) {
            tx = transaction;
            return;
        }
    }
}

void Wallet::removeTransaction(BaseTransaction &transaction) {
    for (auto &tx: transactions) {
        int transactionId = tx.getTransactionId();
        if (transactionId == transaction.getTransactionId()) {
            balance -= tx.getAmount();
            moneySpent -= tx.getNativeAmount();
            bonusBalance -= tx.getAmountBonus();
            transactions.erase(std::remove_if(transactions.begin(), transactions.end(),
                                              [transactionId, &tx](const BaseTransaction &transaction) {
                                                  return tx.getTransactionId() == transactionId;
                                              }), transactions.end());
            return;
        }
    }
}
