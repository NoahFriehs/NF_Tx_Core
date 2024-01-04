//
// Created by nfriehs on 11/7/23.
//

#ifndef NF_TX_CORE_TRANSACTIONMANAGER_H
#define NF_TX_CORE_TRANSACTIONMANAGER_H


#include <vector>
#include <map>
#include <mutex>
#include "Transaction/BaseTransaction.h"
#include "Wallet/Wallet.h"
#include "Wallet/WalletBalance.h"
#include "Price/AssetValue.h"
#include "Enums.h"
#include "TransactionManager/TMState.h"

class TransactionManager {
public:
    TransactionManager();

    explicit TransactionManager(std::vector<BaseTransaction> &transactions);

    ~TransactionManager();

    void setTransactions(std::vector<BaseTransaction> &transactions_, Mode mode);

    void processTransactions();

    void calculateWalletBalances();

    std::vector<std::string> getCurrencies();

    bool isReady() const;

    void setPrices(const std::vector<double> &prices);

    double getTotalMoneySpent() const;

    double getTotalMoneySpentCard() const;

    std::vector<BaseTransaction> getTransactions();

    std::vector<BaseTransaction> getCardTransactions();

    double getTotalValueOfAssets() const;

    double getTotalValueOfAssetsCard() const;

    double getTotalBonus() const;

    double getTotalBonusCard() const;

    double getValueOfAssets(int walletId);

    std::map<std::string, Wallet> getWallets();

    std::map<std::string, Wallet> getCardWallets();

    double getTotalBonus(int walletId);

    double getMoneySpent(int walletId);

    std::unique_ptr<Wallet> getWallet(int walletId);

    void saveData(const std::string &filePath);

    void loadData(const std::string &filePath);

    bool checkSavedData();

    void setWalletData(std::vector<WalletData> _wallets);

    void setCardWalletData(std::vector<WalletData> _cardWallets);

    void setTransactionData(std::vector<TransactionData> txData);

    void setCardTransactionData(std::vector<TransactionData> txData);

    void checkTransactionManagerState();

    void clearAll();

    std::unique_ptr<Wallet> getCardWallet(int walletId);

private:
    bool hasTxData = false;
    bool hasCardTxData = false;
    mutable std::mutex mutex;
    std::vector<BaseTransaction> transactions;
    std::vector<BaseTransaction> cardTransactions;
    std::map<std::string, Wallet> wallets;
    std::map<std::string, Wallet> outWallets;
    std::map<std::string, Wallet> cardWallets;
    WalletsBalance walletsBalance;
    WalletsBalance cardWalletsBalance;
    std::map<std::string, WalletBalance> walletBalanceMap;
    std::map<std::string, WalletBalance> cardWalletBalanceMap;

    AssetValue assetValue;

    std::vector<std::string> currencies;
    std::vector<std::string> cardTxTypes;
    bool isReadyFlag = false;

    void getCurrenciesFromTxs();

    void createWallets();

    void addTransactionsToWallets();

    void vibianPurchase(BaseTransaction &transaction);

    void removeEmptyWallets();

    void removeUnusedTransactions();

    void addCDCTransactionsToWallets();

    void createCardWallets();

    void createCDCWallets();

    void addCardTransactionsToWallets();

    std::string checkCardTxTypes(const std::string &tt, const std::string &txType);

    std::string checkForRefund(std::string &tt);

    Wallet *getNonStrictWallet(std::string &tt);

    TMState getTransactionManagerState();

    void setTransactionManagerState(const TransactionManagerState &state);

    static bool checkIfFileExists(const std::string &file);

};


#endif //NF_TX_CORE_TRANSACTIONMANAGER_H
