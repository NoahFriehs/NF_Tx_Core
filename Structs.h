//
// Created by nfriehs on 12/1/23.
//

#ifndef NF_TX_CORE_STRUCTS_H
#define NF_TX_CORE_STRUCTS_H

#include <string>
#include <ctime>
#include "Enums.h"

struct TransactionData {
    int transactionId{};
    int walletId = -1;
    int fromWalletId{};
    std::string description = {};
    std::tm transactionDate{};
    std::string currencyType = {};
    std::string toCurrencyType = {};
    long double amount{};
    long double toAmount{};
    long double nativeAmount{};
    long double amountBonus{};
    TransactionType transactionType = NONE;
    std::string transactionTypeString = {};
    std::string transactionHash = {};
    bool isOutsideTransaction = false;
    std::string notes = {};
};

struct WalletData {
    int walletId{};
    std::string currencyType = {};
    long double balance{};
    long double nativeBalance{};
    long double bonusBalance{};
    long double moneySpent{};
    bool isOutsideWallet{};
    std::string notes;
};

#endif //NF_TX_CORE_STRUCTS_H
