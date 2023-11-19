//
// Created by nfriehs on 11/7/23.
//

#ifndef NF_TX_CORE_TRANSACTIONMANAGER_H
#define NF_TX_CORE_TRANSACTIONMANAGER_H


#include <vector>
#include <map>
#include <mutex>
#include "BaseTransaction.h"
#include "Wallet.h"
#include "WalletBalance.h"
#include "Price/AssetValue.h"

class TransactionManager {
public:
    TransactionManager();
    explicit TransactionManager(std::vector<BaseTransaction> &transactions);
    ~TransactionManager();

    void processTransactions();

    void calculateWalletBalances();

    std::vector<std::string> getCurrencies();

    bool isReady();

    void setPrices(std::vector<double> prices);

private:
    mutable std::mutex mutex;
    std::vector<BaseTransaction> transactions;
    std::map<std::string, Wallet> wallets;
    std::map<std::string, Wallet> outWallets;
    WalletsBalance walletsBalance;
    std::map<std::string, WalletBalance> walletBalanceMap;

    AssetValue assetValue;

    std::vector<std::string> currencies;
    bool isReadyFlag = false;

    void getCurrenciesFromTxs();

    void createWallets();

    void addTransactionsToWallets();

    void vibianPurchase(BaseTransaction &transaction);

    void removeEmptyWallets();

    void removeUnusedTransactions();
};


#endif //NF_TX_CORE_TRANSACTIONMANAGER_H
