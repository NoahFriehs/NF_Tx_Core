//
// Created by nfriehs on 12/14/23.
//

#ifndef NF_TX_CORE_WALLETSTRUCT_H
#define NF_TX_CORE_WALLETSTRUCT_H

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "../Transaction/TransactionStruct.h"

struct WalletStruct {
    int walletId{};
    std::vector<TransactionStruct> transactions = {};
    std::string currencyType;
    long double balance{};
    long double nativeBalance{};
    long double bonusBalance{};
    long double moneySpent{};
    bool isOutsideWallet{};
    std::string notes;
};

struct CWalletStruct {
    int walletId{};
    CTransactionStruct transactions[MAX_TRANSACTIONS]{};
    int numTransactions{};
    char currencyType[MAX_STRING_LENGTH]{};
    long double balance{};
    long double nativeBalance{};
    long double bonusBalance{};
    long double moneySpent{};
    bool isOutsideWallet{};
    char notes[MAX_STRING_LENGTH]{};

    // Function to convert from a CWalletStruct to WalletStruct
    static WalletStruct convertToWalletStruct(const CWalletStruct &data) {
        WalletStruct tmStruct;
        tmStruct.walletId = data.walletId;
        tmStruct.currencyType = std::string(data.currencyType);
        tmStruct.balance = data.balance;
        tmStruct.nativeBalance = data.nativeBalance;
        tmStruct.bonusBalance = data.bonusBalance;
        tmStruct.moneySpent = data.moneySpent;
        tmStruct.isOutsideWallet = data.isOutsideWallet;
        tmStruct.notes = std::string(data.notes);

        for (int i = 0; i < data.numTransactions; i++) {
            tmStruct.transactions.push_back(CTransactionStruct::convertToTransactionStruct(data.transactions[i]));
        }

        return tmStruct;
    }

    // Function to convert from a WalletStruct to CWalletStruct
    static CWalletStruct convertToCWalletStruct(const WalletStruct &data) {
        CWalletStruct tmStruct;
        tmStruct.walletId = data.walletId;
        stringToCharArray(tmStruct.currencyType, data.currencyType);
        tmStruct.balance = data.balance;
        tmStruct.nativeBalance = data.nativeBalance;
        tmStruct.bonusBalance = data.bonusBalance;
        tmStruct.moneySpent = data.moneySpent;
        tmStruct.isOutsideWallet = data.isOutsideWallet;
        stringToCharArray(tmStruct.notes, data.notes);

        for (int i = 0; i < data.transactions.size(); i++) {
            if (i >= MAX_TRANSACTIONS) {
                FileLog::w("WalletStruct", "Too many transactions to convert to CWalletStruct");
                break;
            }
            tmStruct.transactions[i] = CTransactionStruct::convertToCTransactionStruct(data.transactions[i]);
        }
        tmStruct.numTransactions = data.transactions.size();

        return tmStruct;
    }
};

#endif //NF_TX_CORE_WALLETSTRUCT_H
