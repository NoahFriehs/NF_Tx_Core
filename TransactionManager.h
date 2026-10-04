
#ifndef NF_TX_CORE_TRANSACTIONMANAGER_H
#define NF_TX_CORE_TRANSACTIONMANAGER_H


#include <chrono>
#include <vector>
#include <map>
#include <mutex>
#include "Transaction/BaseTransaction.h"
#include "Wallet/Wallet.h"
#include "Wallet/WalletBalance.h"
#include "Price/AssetValue.h"
#include "Enums.h"

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

    //!
    //! Hard wall-clock budget for the parse/processing running on the
    //! current thread (milliseconds). 0 = no budget, negative = already
    //! expired (used to test the abort path). A single pathological export
    //! must never wedge the (single) JNI thread that runs all of this.
    static void setParseBudgetMs(long long ms);

    static bool parseBudgetExceeded();

    //! Return a snapshot of the serializable state
    TransactionManagerState getTransactionManagerState();

    //! Replace the serializable state (clears, then restores)
    void setTransactionManagerState(const TransactionManagerState &state);

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

    //!
    //! Same accounting as getTotalMoneySpent (signed native amounts of the
    //! inner wallets), bucketed by calendar month, oldest first:
    //! "YYYY-MM;123.45" per month.
    std::vector<std::string> getMoneySpentSeries() const;

    //!
    //! Daily accounting series of the inner wallets (EUR excluded, the same
    //! scope as the money-spent/asset cards via
    //! WalletsBalance::fillFromWalletBalanceMap), reconstructed by replaying
    //! the per-wallet ledgers chronologically. One line per active day per
    //! series, oldest first:
    //!   "spent;YYYY-MM-DD;v"   money spent during that day (flow)
    //!   "value;YYYY-MM-DD;v"   total asset value at end of day, EUR at the
    //!                          current prices (stock)
    //!   "pl;YYYY-MM-DD;v"      value minus cumulative money spent (stock)
    //!   "bonus;YYYY-MM-DD;v"   total bonus value at end of day, EUR (stock)
    //! The last point of value/pl/bonus equals the current card total by
    //! construction (telescoping: the ledger deltas sum to the wallet state).
    std::vector<std::string> getDailySeries() const;

    //!
    //! Per-wallet daily series of the inner wallets (same scope as
    //! getDailySeries), in UNPRICED token amounts: one line per (wallet,
    //! active day) in which the wallet's totals changed, oldest first:
    //!   "CURRENCY;YYYY-MM-DD;runningBalance;runningBonus"
    //! The running values carry over across gaps, so "last line for a
    //! currency at or before date D" is the position as of D. A consumer
    //! values each point with the prices valid at that time (historical
    //! prices for the chart panels); getDailySeries above instead fixes
    //! the current prices.
    std::vector<std::string> getDailyWalletSeries() const;

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

    //! Return the outside wallets (see getWallets())
    const std::map<std::string, Wallet> & getOutWallets();

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
    static thread_local long long t_parseBudgetMs;
    static thread_local std::chrono::steady_clock::time_point t_parseDeadline;

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
    Mode currentMode = Default;
    size_t failedLines = 0;
    bool upgradedLegacy_ = false;   // last load used v2 files: next save upgrades them

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

    //! Add BlockPit transactions to the wallets
    void addBlockPitTransactionsToWallets();

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

    //! Get or create a wallet in the given map (operator[] on the maps
    //! would default-construct wallets with a fresh, non-monotonic id)
    Wallet &getOrCreateWallet(std::map<std::string, Wallet> &target, const std::string &key);

    //! Check if the file exists
    static bool checkIfFileExists(const std::string &file);

};


#endif //NF_TX_CORE_TRANSACTIONMANAGER_H
