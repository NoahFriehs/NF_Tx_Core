//
// Created by nfriehs on 11/18/23.
//

#include "Wallet.h"
#include <string>
#include <map>

#ifndef NF_TX_CORE_WALLETBALANCE_H
#define NF_TX_CORE_WALLETBALANCE_H

struct WalletBalance {
    int walletId;
    std::string currencyType;
    long double balance;
    long double nativeBalance;
    long double bonusBalance;
    long double nativeBonusBalance;
    long double moneySpent;

    WalletBalance *fillFromWallet(Wallet *wallet) {
        if (wallet == nullptr) {
            return nullptr;
        }

        walletId = wallet->getWalletId();
        balance = wallet->getBalance();
        nativeBalance = wallet->getNativeBalance();
        bonusBalance = wallet->getBonusBalance();
        moneySpent = wallet->getMoneySpent();
        currencyType = wallet->getCurrencyType();

        return this;
    }
};

struct WalletsBalance {

    long double nativeBalance = 0.0;
    long double nativeBonusBalance = 0.0;
    long double moneySpent = 0.0;

    void fillFromWalletBalances(const std::vector<WalletBalance> &walletBalances) {
        for (const auto &walletBalance: walletBalances) {
            nativeBalance += walletBalance.nativeBalance;
            moneySpent += walletBalance.moneySpent;
            nativeBonusBalance += walletBalance.nativeBonusBalance;
        }
    }

    void fillFromWalletBalanceMap(const std::map<std::string, WalletBalance> &walletBalanceMap) {
        for (const auto &pair: walletBalanceMap) {
            const auto &walletBalance = pair.second;
            nativeBalance += walletBalance.nativeBalance;
            moneySpent += walletBalance.moneySpent;
            nativeBonusBalance += walletBalance.nativeBonusBalance;
        }
    }
};


#endif //NF_TX_CORE_WALLETBALANCE_H
