
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

    //! Parse the Curve card export row (11 columns)
    void parseCurve(const std::string &txString);

    void parseKraken(const std::string &txString);

    void parseBlockPit(const std::string &txString);

    //! Return the transaction id
    int getTransactionId() const;

    //! Set the transaction amount to the amount bonus
    void setAmountToAmountBonus();

    //! Set the wallet id
    void setWalletId(int id);

    //! Set the from wallet id
    void setFromWalletId(int id);

    //! Return the currency type
    std::string getCurrencyType() const;

    //! Return the transaction amount
    long double getAmount() const;

    //! Return the transaction native amount
    long double getNativeAmount() const;

    //! Return the fee amount (BlockPit: fee paid in this transaction)
    long double getFeeAmount() const;

    //! Return the fee asset (BlockPit: currency in which fee was paid)
    std::string getFeeAsset() const;

    //! Return the transaction type
    TransactionType getTransactionType() const;

    //! Return the transaction to currency type
    std::string getToCurrencyType() const;

    //! Return the transaction Type as a string
    std::string getTransactionTypeString() const;

    //! Return the wallet id
    [[nodiscard]] int getWalletId() const;

    //! Return the amount bonus
    [[nodiscard]] long double getAmountBonus() const;

    //! Return the toAmount
    [[nodiscard]] long double getToAmount() const;

    //! Return if the transaction is excluded from the wallet totals
    //! (kept in the lists; the row applies no balance/spend/bonus delta)
    [[nodiscard]] bool isExcludedFromTotals() const;

    //! Return the transaction data
    TransactionData getTransactionData() const;

    //! Return the transaction struct
    TransactionStruct getTransactionStruct();

    //! Fill the transaction from the transaction struct
    void fromTransactionStruct(const TransactionStruct &data);

    //! Set the transaction amount
    void setAmount(long double amount_);

    //! Set the transaction native amount
    void setNativeAmount(long double nativeAmount_);

    //! Set the currency type
    void setCurrencyType(const std::string &currencyType_);

    //! Set the to amount
    void setToAmount(long double toAmount_);

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
    std::string feeAsset = {};
    long double amount{};
    long double toAmount{};
    long double nativeAmount{};   // Amount in native currency: USD, EUR, etc.
    long double amountBonus{};
    long double feeAmount{};
    TransactionType transactionType = NONE;
    std::string transactionTypeString = {};

    std::string transactionHash = {};
    bool isOutsideTransaction = false;
    bool excludedFromTotals = false;   // wallet totals skip this row
    std::string notes = {};

public:
    //! Check if a currency string represents a fiat currency
    static bool isFiatCurrency(const std::string &currency);


};


#endif //NF_TX_CORE_BASETRANSACTION_H
