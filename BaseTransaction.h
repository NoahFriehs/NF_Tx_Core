//
// Created by nfriehs on 11/7/23.
//

#ifndef NF_TX_CORE_BASETRANSACTION_H
#define NF_TX_CORE_BASETRANSACTION_H


#include <string>
#include <ctime>
#include "Enums.h"
#include "Structs.h"

class BaseTransaction {

public:
    BaseTransaction();

    ~BaseTransaction();


    void parseCDC(const std::string &txString);

    void setAmountToAmountBonus();

    void setWalletId(int id);

    void setFromWalletId(int id);

    std::string getCurrencyType();

    long double getAmount();

    long double getNativeAmount();

    TransactionType getTransactionType();

    std::string getToCurrencyType();

    std::string getTransactionTypeString();

    int getWalletId();

    long double getAmountBonus();

    long double getToAmount();

    TransactionData getTransactionData();

private:
    int transactionId{};
    int walletId = -1;
    int fromWalletId{};
    std::string description = {};
    std::tm transactionDate{};
    std::string currencyType = {};
    std::string toCurrencyType = {};
    long double amount{};
    long double toAmount{};
    long double nativeAmount{};   // Amount in native currency: USD, EUR, etc.
    long double amountBonus{};
    TransactionType transactionType = NONE;
    std::string transactionTypeString = {};
    std::string transactionHash = {};
    bool isOutsideTransaction = false;
    std::string notes = {};


};


#endif //NF_TX_CORE_BASETRANSACTION_H
