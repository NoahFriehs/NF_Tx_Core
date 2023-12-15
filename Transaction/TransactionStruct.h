//
// Created by nfriehs on 12/14/23.
//

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

struct CTransactionStruct {
    int transactionId{};
    int walletId = -1;
    int fromWalletId{};
    char description[MAX_STRING_LENGTH]{};
    char dateTimeStr[MAX_DATE_LENGTH]{};
    char currencyType[MAX_WALLETS]{};
    char toCurrencyType[MAX_WALLETS]{};
    long double amount{};
    long double toAmount{};
    long double nativeAmount{};
    long double amountBonus{};
    TransactionType transactionType = NONE;
    char transactionTypeString[20]{}; // Adjust size as needed
    char transactionHash[64]{}; // Assuming a fixed length for the hash
    bool isOutsideTransaction = false;
    char notes[255]{}; // Assuming a maximum length for the notes

    // Function to convert from a CTransactionStruct to TransactionStruct
    static TransactionStruct convertToTransactionStruct(const CTransactionStruct& data) {
        TransactionStruct tmStruct;
        tmStruct.transactionId = data.transactionId;
        tmStruct.walletId = data.walletId;
        tmStruct.fromWalletId = data.fromWalletId;
        tmStruct.description = std::string(data.description);
        tmStruct.transactionDate = TimestampConverter::stringToTm(data.dateTimeStr);
        tmStruct.currencyType = std::string(data.currencyType);
        tmStruct.toCurrencyType = std::string(data.toCurrencyType);
        tmStruct.amount = data.amount;
        tmStruct.toAmount = data.toAmount;
        tmStruct.nativeAmount = data.nativeAmount;
        tmStruct.amountBonus = data.amountBonus;
        tmStruct.transactionType = data.transactionType;
        tmStruct.transactionTypeString = std::string(data.transactionTypeString);
        tmStruct.transactionHash = std::string(data.transactionHash);
        tmStruct.isOutsideTransaction = data.isOutsideTransaction;
        tmStruct.notes = std::string(data.notes);

        return tmStruct;
    }

    // Function to convert from a TransactionStruct to CTransactionStruct
    static CTransactionStruct convertToCTransactionStruct(const TransactionStruct& originalStruct) {
        CTransactionStruct cTMStruct;
        // Copying data from original to new struct
        cTMStruct.transactionId = originalStruct.transactionId;
        cTMStruct.walletId = originalStruct.walletId;
        cTMStruct.fromWalletId = originalStruct.fromWalletId;
        stringToCharArray(cTMStruct.description, originalStruct.description);
        // Format the date and time as a string
        char _dateTimeStr[MAX_DATE_LENGTH];
        std::strftime(_dateTimeStr, sizeof(_dateTimeStr), "%Y-%m-%d %H:%M:%S",
                      &originalStruct.transactionDate);
        std::strcpy(cTMStruct.dateTimeStr, _dateTimeStr);
        stringToCharArray(cTMStruct.currencyType, originalStruct.currencyType);
        stringToCharArray(cTMStruct.toCurrencyType, originalStruct.toCurrencyType);
        cTMStruct.amount = originalStruct.amount;
        cTMStruct.toAmount = originalStruct.toAmount;
        cTMStruct.nativeAmount = originalStruct.nativeAmount;
        cTMStruct.amountBonus = originalStruct.amountBonus;
        cTMStruct.transactionType = originalStruct.transactionType;
        stringToCharArray(cTMStruct.transactionTypeString, originalStruct.transactionTypeString);
        stringToCharArray(cTMStruct.transactionHash, originalStruct.transactionHash);
        cTMStruct.isOutsideTransaction = originalStruct.isOutsideTransaction;
        stringToCharArray(cTMStruct.notes, originalStruct.notes);

        return cTMStruct;
    }
};

#endif //NF_TX_CORE_TRANSACTIONSTRUCT_H
