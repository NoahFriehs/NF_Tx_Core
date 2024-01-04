//
// Created by nfriehs on 10/31/23.
//

#ifndef NF_TX_CORE_WALLET_H
#define NF_TX_CORE_WALLET_H


#include <vector>
#include <memory>
#include "../Transaction/BaseTransaction.h"
#include "WalletStruct.h"

class Wallet {
public:
    Wallet();

    explicit Wallet(std::string currencyType);

    ~Wallet();

    //! Add a transaction to the wallet
    bool addTransaction(BaseTransaction &transaction, bool overrideTTS = false);

    //! Add a transaction to the wallet to withdraw
    bool withdraw(BaseTransaction &transaction);

    //void addBonus(BaseTransaction &transaction);

    void updateTransaction(BaseTransaction &transaction);

    void removeTransaction(BaseTransaction &transaction);

    //! Return the transactions
    std::vector<BaseTransaction> getTransactions();

    //! Set is outside wallet
    void setIsOutWallet(bool isOut);

    //! Return the wallet id
    [[nodiscard]]
    int getWalletId() const;

    //! Return the native balance
    long double getNativeBalance() const;

    //! Return the bonus balance
    long double getBonusBalance() const;

    //! Return the money spent
    long double getMoneySpent() const;

    //! Return the balance
    long double getBalance() const;

    //! Return the currency type
    [[nodiscard]] std::string getCurrencyType() const;

    //! Add a to transaction
    void addToTransaction(BaseTransaction &transaction);

    //! Return WalletData
    std::unique_ptr<WalletData> getWalletData();

    //! Return WalletStruct
    WalletStruct *getWalletStruct();

    //! Fill from WalletStruct
    void setWalletData(const WalletStruct &data);

    //! Set the currency type
    void setCurrencyType(std::string currencyType_);

    //! Return if the wallet is outside
    [[nodiscard]]
    bool getIsOutWallet() const;

private:
    int walletId{};
    std::vector<BaseTransaction> transactions = {};
    std::string currencyType;
    long double balance{};
    long double nativeBalance{};
    long double bonusBalance{};
    long double moneySpent{};
    bool isOutsideWallet{};
    std::string notes;
};


#endif //NF_TX_CORE_WALLET_H
