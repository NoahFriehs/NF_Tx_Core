
#ifndef NF_TX_CORE_TRANSACTIONMANAGER_H
#define NF_TX_CORE_TRANSACTIONMANAGER_H


#include <chrono>
#include <vector>
#include <map>
#include <mutex>
#include <set>
#include "Transaction/BaseTransaction.h"
#include "Wallet/Wallet.h"
#include "Wallet/WalletBalance.h"
#include "Price/AssetValue.h"
#include "Enums.h"

class TransactionManager {
public:
    //! Exact-tuple identity of a ledger row (the multi-file append
    //! contract). The tuple lives per bucket (the mode is implicit), and
    //! the key sets are bookkeeping over the rows the manager holds -
    //! rebuilt on every parse/append/restore, cleared by clearAll.
    //! In-memory only: no stored format is affected.
    struct AppendKey {
        //! date parts folded into one comparable field (ymdhms)
        long long time{};
        std::string description;
        long double amount{};
        std::string currency;

        bool operator<(const AppendKey &o) const {
            if (time != o.time) return time < o.time;
            if (description != o.description) return description < o.description;
            // NaNs are normalized to 0 in appendKeyOf, so the strict
            // weak ordering is total.
            if (amount != o.amount) return amount < o.amount;
            return currency < o.currency;
        }
    };

    TransactionManager();

    explicit TransactionManager(std::vector<BaseTransaction> &transactions);

    ~TransactionManager();

    //! Set the transactions (REPLACE of the bucket for [mode]; the
    //! append-tuple bookkeeping of that bucket is rebuilt from [transactions_])
    void setTransactions(std::vector<BaseTransaction> &transactions_, Mode mode);

    //! Appends [txs] (already parsed in [mode]) to the live manager:
    //! rows whose exact tuple (date, description, amount, currency) is
    //! already present in the bucket are SKIPPED and counted
    //! (getDuplicateSkips) instead of double-accounting an overlapping
    //! selection - exact only, never fuzzy. The mode is implicit in the
    //! bucket (crypto/card), so rows can never collide across buckets.
    //! Only the NEW rows are assigned to their wallets (re-running the
    //! full processTransactions() would add the existing rows' ledger
    //! deltas a second time). Returns the number of rows actually added.
    size_t appendTransactions(std::vector<BaseTransaction> &txs, Mode mode);

    //! Exact-tuple bookkeeping of one row (internal: the mutating paths
    //! call it while holding the mutex; read-only access is safe)
    static AppendKey appendKeyOf(const BaseTransaction &tx);

    bool hasAppendKey(bool isCard, const BaseTransaction &tx) const;

    void registerAppendKeys(bool isCard, const std::vector<BaseTransaction> &txs);

    //! Rows skipped as exact-tuple duplicates by appendTransactions since
    //! the last manager create/clearAll (0 for a single-file import)
    size_t getDuplicateSkips() const { return appendedDuplicateSkips_; }

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

    //! Record that the parse running on the current thread was cut by the
    //! hard budget. unprocessedRows: the input rows the parser never read,
    //! or 0 when every row was parsed but the wallet build stopped.
    static void markLastParseCut(int unprocessedRows);

    //! 1 when the last parse on the current thread was cut by the budget.
    static bool wasLastParseCutByBudget();

    //! Input rows the budget prevented from reading in the last (cut) parse.
    static int lastParseUnprocessedRows();

    //! Return a snapshot of the serializable state
    TransactionManagerState getTransactionManagerState();

    //! Replace the serializable state (clears, then restores)
    void setTransactionManagerState(const TransactionManagerState &state);

    //!
    //! The user-owned balance-sheet section of the state file (the core-
    //! serialized sheet text). The manager is the TRANSPORT here: the
    //! DataHolder owns the sheet and injects its text before every save; the
    //! load path reads it back. clearAll() deliberately does NOT touch it -
    //! the sheet is user-entered data, not import state (the restore
    //! sequence init -> clearAll -> set*Data must never wipe it).
    void setBalanceSheetText(const std::string &text);

    //! The sheet section text of the last load/save (the default-constructed
    //! manager has the empty section).
    std::string getBalanceSheetText() const;

    //!
    //! The directory the last save/load ran against: the target of the
    //! core's own sheet auto-saves (a sheet mutation re-saves the store
    //! there so the user entry survives the next cold start without the
    //! app doing anything). Empty until a save or load has run.
    std::string getSaveDir() const;

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

    //!
    //! Money spent (the same inner-wallet accounting as getDailySeries)
    //! bucketed into fixed [windowSeconds] slots from the epoch (UTC): one
    //! line per slot that has activity, oldest first:
    //!   "YYYY-MM-DD HH:MM;v"   v = money spent during that slot (flow)
    //! [fromEpoch] (epoch seconds, 0 = no limit) suppresses the emission
    //! of slots that start before it; earlier activity still accumulates
    //! the per-wallet running state (which the wallet sibling below emits).
    std::vector<std::string> getSubDailySpentSeries(
            int windowSeconds, long long fromEpoch = 0) const;

    //!
    //! Per-wallet series in UNPRICED token amounts (the windowed sibling of
    //! getDailyWalletSeries): one line per (wallet, changed slot), oldest
    //! first:
    //!   "CURRENCY;YYYY-MM-DD HH:MM;runningBalance;runningBonus"
    //! The slot is the transaction epoch floored to [windowSeconds]; the
    //! running values carry over across gaps (and across the [fromEpoch]
    //! cut, see the spent sibling), so "last line for a currency at or
    //! before T" is the position as of T. A consumer prices each point with
    //! the price valid at the slot.
    //!
    //! A LIMITED window ([fromEpoch] > 0) additionally opens with one line
    //! per currency whose running position is non-zero at the window start:
    //! the pre-cumulated state at the fromEpoch slot key, BEFORE that slot's
    //! own deltas. Without it, a position that predates the window would be
    //! unrecoverable (only CHANGED slots are emitted, and the rows carry
    //! running values, not deltas). A fully stale export (no activity at or
    //! after the cut) yields only those opening lines. [fromEpoch] must be
    //! grid-aligned (a multiple of [windowSeconds]) - the caller floors it;
    //! [fromEpoch] = 0 emits no opening line (unlimited series unchanged).
    std::vector<std::string> getSubDailyWalletSeries(
            int windowSeconds, long long fromEpoch = 0) const;

    //! Return the total money spent on card
    double getTotalMoneySpentCard() const;

    //! Return the transactions (internal storage, do not keep the reference
    //! across calls that modify the manager)
    const std::vector<BaseTransaction> & getTransactions();

    //! Return the card transactions (internal storage, see getTransactions())
    const std::vector<BaseTransaction> & getCardTransactions();

    //! Return the total value of assets
    double getTotalValueOfAssets() const;

    //!
    //! The total value of ALL inner wallets, the internal "EUR" cash
    //! buffer INCLUDED (unlike [getTotalValueOfAssets], which excludes it
    //! like every card/donut aggregate): the net-worth "tracked assets"
    //! figure the balance-sheet snapshot injects - cash counts toward net
    //! worth, the buffer is the user's tracked cash.
    double getTotalValueOfAssetsIncludingBuffer() const;

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

    //! Save the data to the given directory (created if it does not exist
    //! yet). Returns true when all three files were written.
    bool saveData(const std::string &dirPath);

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
    //! Per-parse result state (see markLastParseCut): the cut sites set it,
    //! setParseBudgetMs with a non-zero budget resets it - a parse never
    //! inherits a previous parse's cut state, and the end-of-parse release
    //! call (0) keeps it readable until the caller has consumed it.
    static thread_local bool t_lastParseCutByBudget;
    static thread_local int t_lastParseUnprocessedRows;

    std::set<AppendKey> appendCryptoKeys_;
    std::set<AppendKey> appendCardKeys_;
    size_t appendedDuplicateSkips_ = 0;

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
    bool upgradedLegacy_ = false;   // last load used v2/v3/v4 files: next save upgrades them

    // The balance-sheet section transport (see the public accessors:
    // the DataHolder owns the sheet, this field carries it to the state
    // file). clearAll() must not touch it (user data, not import state).
    std::string balanceSheetText_;
    std::string saveDir_;

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

    //! Add Crypto transactions to the wallets (the [begin, begin+count) range
    //! of the crypto bucket = the rows assigned in this pass; full range for
    //! an init, the newly appended rows for the multi-file append)
    void addCDCTransactionsToWallets(size_t begin, size_t count);

    //! Add BlockPit transactions to the wallets (range contract as above)
    void addBlockPitTransactionsToWallets(size_t begin, size_t count);

    //! Create the card wallets
    void createCardWallets();

    //! Create the crypto wallets
    void createCDCWallets();

    //! Add the card transactions to the wallets (range contract as above)
    void addCardTransactionsToWallets(size_t begin, size_t count);

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
