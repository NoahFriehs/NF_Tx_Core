//
// Created by nfriehs on 11/7/23.
//

#ifndef NF_TX_CORE_BASETRANSACTION_H
#define NF_TX_CORE_BASETRANSACTION_H


#include <string>
#include <ctime>
#include "../Enums.h"
#include "../Structs.h"
#include "TransactionStruct.h"

class BaseTransaction {

public:
    BaseTransaction();

    ~BaseTransaction();

    //! Parse the transaction string
    void parseCDC(const std::string &txString);

    //! Parse the card transaction string
    void parseCard(const std::string &txString);

    void parseKraken(const std::string &txString);

    //! Return the transaction id
    int getTransactionId() const;

    //! Set the transaction amount to the amount bonus
    void setAmountToAmountBonus();

    //! Set the wallet id
    void setWalletId(int id);

    //! Set the from wallet id
    void setFromWalletId(int id);

    //! Return the currency type
    std::string getCurrencyType();

    //! Return the transaction amount
    long double getAmount() const;

    //! Return the transaction native amount
    long double getNativeAmount() const;

    //! Return the transaction type
    TransactionType getTransactionType();

    //! Return the transaction to currency type
    std::string getToCurrencyType();

    //! Return the transaction Type as a string
    std::string getTransactionTypeString();

    //! Return the wallet id
    [[nodiscard]] int getWalletId() const;

    //! Return the amount bonus
    [[nodiscard]] long double getAmountBonus() const;

    //! Return the toAmount
    [[nodiscard]] long double getToAmount() const;

    //! Return the transaction data
    TransactionData getTransactionData();

    //! Return the transaction struct
    TransactionStruct getTransactionStruct();

    //! Fill the transaction from the transaction struct
    void fromTransactionStruct(const TransactionStruct &data);

    //! Set the transaction id counter
    static void setTxIdCounter(int txIdCounter_);

    //! Return the transaction id counter
    static int getTxIdCounter();

    //! Fill the transaction from the transaction data
    void setTransactionData(const TransactionStruct &txStruct);

    //! Set the transaction type String
    void setTransactionTypeString(const std::string &transactionTypeStringToSet);

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
    long double feeAmount{};
    TransactionType transactionType = NONE;
    std::string transactionTypeString = {};

    std::string transactionHash = {};
    bool isOutsideTransaction = false;
    std::string notes = {};


};


#endif //NF_TX_CORE_BASETRANSACTION_H
