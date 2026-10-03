
#ifndef NF_TX_CORE_TRANSACTIONSTRUCT_H
#define NF_TX_CORE_TRANSACTIONSTRUCT_H

#include "../Enums.h"
#include "../Util/CharUtil.h"
#include "../Util/Util.h"
#include <string>
#include <ctime>
#include <cstring>

struct TransactionStruct {
    int transactionId{};
    int walletId = -1;
    int fromWalletId{};
    std::string description = {};
    std::tm transactionDate{};
    std::string currencyType = {};
    std::string toCurrencyType = {};
    std::string feeAsset = {};
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

#endif //NF_TX_CORE_TRANSACTIONSTRUCT_H
