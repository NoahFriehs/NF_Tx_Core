
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

    //! Set the transactions
    void setTransactions(std::vector<BaseTransaction> &transactions_, Mode mode);

    //! Number of CSV lines that could not be parsed (set after parsing)
    void setFailedLines(size_t n) { failedLines = n; }

    size_t getFailedLines() const { return failedLines; }

    //! Process the transactions
    void processTransactions();

    //! Calculate the wallet balances
    void calculateWalletBalances();

    //! Return the Currencies (internal storage, do not keep the reference
    //! across calls that modify the manager)
    const std::vector<std::string> & getCurrencies();

    //! Return if the TransactionManager is ready
    bool isReady() const;

    //! Set the prices, must be in the same order as getCurrencies().
    void setPrices(const std::vector<double> &prices);

    //! Return the total money spent
    double getTotalMoneySpent() const;

    //! Return the total money spent on card
    double getTotalMoneySpentCard() const;

    //! Return the transactions (internal storage, do not keep the reference
    //! across calls that modify the manager)
    const std::vector<BaseTransaction> & getTransactions();

    //! Return the card transactions (internal storage, see getTransactions())
    const std::vector<BaseTransaction> & getCardTransactions();

    //! Return the total value of assets
    double getTotalValueOfAssets() const;

    //! Return the total value of assets on card
    double getTotalValueOfAssetsCard() const;

    //! Return the total bonus
    double getTotalBonus() const;

    //! Return the total bonus on card
    double getTotalBonusCard() const;

    //! Return the value of assets of the given wallet
    double getValueOfAssets(int walletId);

    //! Return all the wallets (internal storage, see getTransactions())
    const std::map<std::string, Wallet> & getWallets();

    //! Return all the card wallets (internal storage, see getTransactions())
    const std::map<std::string, Wallet> & getCardWallets();

    //! Return the bonus of the given wallet
    double getTotalBonus(int walletId);

    //! Return the money spent of the given wallet
    double getMoneySpent(int walletId);

    //! Return the wallet (also card wallet), nullptr if not found.
    //! The pointer is owned by the manager and valid until the next call
    //! that modifies the wallets.
    Wallet *getWallet(int walletId);

    //! Save the data to the given directory
    void saveData(const std::string &dirPath);

    //! Load the data from the given directory
    void loadData(const std::string &dirPath);

    //! Check if the data is saved in the given directory
    bool checkSavedData(const std::string &dirPath);

    //! Set the wallet data
    void setWalletData(const std::vector<WalletData> &_wallets);

    //! Set the card wallet data
    void setCardWalletData(const std::vector<WalletData> &_cardWallets);

    //! Set the transaction data
    void setTransactionData(const std::vector<TransactionData> &txData);

    //! Set the card transaction data
    void setCardTransactionData(const std::vector<TransactionData> &txData);

    //! Checks the state of the TransactionManager
    void checkTransactionManagerState();

    //! Clear all the data
    void clearAll();

    //! Return the card wallet (see getWallet())
    Wallet *getCardWallet(int walletId);

    //! \brief Returns the active modes. (1 = Crypto, 2 = Card, 3 = Crypto + Card)
    int getActiveModes() const;

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
    size_t failedLines = 0;

    //! Get the currencies from the transactions
    void getCurrenciesFromTxs();

    //! Create the wallets
    void createWallets();

    //! Add the transactions to the wallets
    void addTransactionsToWallets();

    //! Add a vibian purchase to the wallets (Crypto)
    void vibianPurchase(BaseTransaction &transaction);

    //! Remove the empty wallets
    void removeEmptyWallets();

    //! Remove the unused transactions
    void removeUnusedTransactions();

    //! Add Crypto transactions to the wallets
    void addCDCTransactionsToWallets();

    //! Create the card wallets
    void createCardWallets();

    //! Create the crypto wallets
    void createCDCWallets();

    //! Add the card transactions to the wallets
    void addCardTransactionsToWallets();

    //! Card: Check cardTxTypes for tt and remove it and add txType to cardTxTypes
    std::string checkCardTxTypes(const std::string &tt, const std::string &txType);

    //! Card: Checks if the transaction is a refund and replace string
    std::string checkForRefund(std::string &tt);

    //! Card: Get non strict wallet
    Wallet *getNonStrictWallet(std::string &tt);

    //! Return the state of the TransactionManager
    TransactionManagerState getTransactionManagerState();

    //! Get or create a wallet in the given map (operator[] on the maps
    //! would default-construct wallets with a fresh, non-monotonic id)
    Wallet &getOrCreateWallet(std::map<std::string, Wallet> &target, const std::string &key);

    //! Set the state of the TransactionManager
    void setTransactionManagerState(const TransactionManagerState &state);

    //! Check if the file exists
    static bool checkIfFileExists(const std::string &file);

};


#endif //NF_TX_CORE_TRANSACTIONMANAGER_H
