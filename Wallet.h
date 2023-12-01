//
// Created by nfriehs on 10/31/23.
//

#ifndef NF_TX_CORE_WALLET_H
#define NF_TX_CORE_WALLET_H


#include <vector>
#include "BaseTransaction.h"

class Wallet {
public:
    Wallet();

    Wallet(std::string currencyType);

    ~Wallet();

    bool addTransaction(BaseTransaction &transaction);
    bool withdraw(BaseTransaction &transaction);
    void addBonus(BaseTransaction &transaction);
    void updateTransaction(BaseTransaction &transaction);
    void removeTransaction(BaseTransaction &transaction);
    std::vector<BaseTransaction> getTransactions();

    void setIsOutWallet(bool b);

    int getWalletId();

    long double getNativeBalance() const;

    long double getBonusBalance() const;

    long double getMoneySpent() const;

    long double getBalance() const;


    std::string getCurrencyType() const;

    void addToTransaction(BaseTransaction &transaction);

    WalletData getWalletData();

private:
    int walletId{};
    std::vector<BaseTransaction> transactions = {};
    std::string currencyType = {};
    long double balance{};
    long double nativeBalance{};
    long double bonusBalance{};
    long double moneySpent{};
    bool isOutsideWallet{};
    std::string notes;
};


#endif //NF_TX_CORE_WALLET_H
