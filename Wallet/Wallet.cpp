
#include "Wallet.h"
#include "../FileLog.h"

#include <utility>
#include <memory>
#include <algorithm>


Wallet::Wallet() {
    walletId = walletIdCounter.fetch_add(1);
}

Wallet::~Wallet() = default;

Wallet::Wallet(std::string currencyType) : Wallet() {
    FileLog::v("Wallet", "Creating wallet with currency type: " + currencyType);
    this->currencyType = std::move(currencyType);
}

void Wallet::setWalletIdCounter(int counter) {
    // Forward-only: raising the counter is allowed, resetting it would re-use
    // wallet ids. Use a CAS loop so concurrent initializers do not lose ids.
    int current = walletIdCounter.load();
    while (counter > current &&
           !walletIdCounter.compare_exchange_weak(current, counter)) {
    }
}

int Wallet::getWalletIdCounter() {
    return walletIdCounter.load();
}

void Wallet::setIsOutWallet(bool isOut) {
    isOutsideWallet = isOut;
}

bool Wallet::addTransaction(BaseTransaction &transaction, bool overrideTTS) {
    transaction.setWalletId(walletId);
    if (overrideTTS) transaction.setTransactionTypeString(currencyType);
    transactions.push_back(transaction);
    balance += transaction.getAmount();
    moneySpent += transaction.getNativeAmount();
    bonusBalance += transaction.getAmountBonus();
    return true;
}

int Wallet::getWalletId() const {
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

std::unique_ptr<WalletData> Wallet::getWalletData() const {
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
    currencyType = std::move(currencyType_);
}

std::unique_ptr<WalletStruct> Wallet::getWalletStruct() {
    auto data = std::make_unique<WalletStruct>();
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

bool Wallet::getIsOutWallet() const {
    return isOutsideWallet;
}

void Wallet::updateTransaction(BaseTransaction &transaction) {
    int id = transaction.getTransactionId();
    for (auto &tx: transactions) {
        if (tx.getTransactionId() == id) {
            tx = transaction;
            return;
        }
    }
}

void Wallet::removeTransaction(BaseTransaction &transaction) {
    int id = transaction.getTransactionId();
    // Plain index scan: erase-remove with a capture referencing the loop
    // element is invalid, and erasing invalidates iterators and references.
    for (size_t i = 0; i < transactions.size(); i++) {
        if (transactions[i].getTransactionId() == id) {
            const auto &tx = transactions[i];
            balance -= tx.getAmount();
            moneySpent -= tx.getNativeAmount();
            bonusBalance -= tx.getAmountBonus();
            transactions.erase(transactions.begin() + static_cast<long>(i));
            return;
        }
    }
}
