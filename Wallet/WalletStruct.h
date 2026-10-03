
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

#endif //NF_TX_CORE_WALLETSTRUCT_H
