#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"  // Ignore warning about C linkage
#ifndef NF_TX_CORE_LIBRARY_H
#define NF_TX_CORE_LIBRARY_H

#include <vector>
#include <string>

#ifdef ANDROID
#pragma message("Building for Android")
#endif

extern "C" {

//! \brief Initializes the library with the given log file path and load directory path.
bool init(const std::string &logFilePath, const std::string &loadDirPath);

//! \brief Initializes the library with the given data, mode, log file path and load directory path.
// parseBudgetMs: hard wall-clock budget for the whole parse (0 = none).
bool initWithData(const std::vector<std::string> &data, int mode,
                  const std::string &logFilePath, long long parseBudgetMs);

//! \brief Appends the given data to the ALREADY-INITIALIZED library (the
//! multi-file combined view): the rows are parsed like initWithData (same
//! parser, same parseBudgetMs deadline) and appended to the live manager's
//! bucket for [mode]. Rows whose exact tuple (date, description, amount,
//! currency) is already present in that bucket are skipped and counted
//! (TransactionManager::getDuplicateSkips) instead of double-accounting the
//! overlap - mixed-mode appends (card onto crypto or vice versa) are
//! supported; the mode is implicit in the bucket.
// The init-then-append ordering is the caller's guarantee (the Kotlin side
// sequences it on the single core thread); on an uninitialized core this
// logs and returns false (no throw across the C boundary). A parse that
// yields zero rows returns false, like initWithData.
bool appendWithData(const std::vector<std::string> &data, int mode,
                    const std::string &logFilePath, long long parseBudgetMs);

//! \brief Saves the current state to the given directory (created if
//! missing; dir path with trailing slash). Returns true when the save
//! succeeded.
bool save(const std::string &filePath);

//! \brief Loads the saved data from the given dir path.
void loadData(const std::string &dirPath);

//! \brief Returns the active modes. (1 = Crypto, 2 = Card, 3 = Crypto + Card)
int getActiveModes();

//! \brief 1 when the last parse on the calling thread was cut short by the
//! hard parse budget, 0 otherwise (or when no parse ran yet on this thread).
//! The state is per-thread and per-parse - the budget setter resets it, so
//! it must be read on the parse thread right after initWithData /
//! appendWithData returned.
int lastParseBudgetCut(void);

//! \brief Rows of the last (cut) parse the budget prevented from reading:
//! the parser stop reports the input rows never read (the cut row
//! included); a wallet-build stop reports 0 (every row was parsed, the
//! aggregation stopped). Read it together with lastParseBudgetCut.
int lastParseUnprocessedRows(void);

//void calculate();

//! \brief Resets the library.
void clearAll();

//! \brief Set the wallet data formatted in XML.
void setWalletData(const std::vector<std::string> &data);

//! \brief Set the card wallet data formatted in XML.
void setCardWalletData(const std::vector<std::string> &data);

//! \brief Set the transaction data formatted in XML.
void setTransactionData(const std::vector<std::string> &data);

//! \brief Set the card transaction data formatted in XML.
void setCardTransactionData(const std::vector<std::string> &data);

//! \brief Calculates the balances of the wallets.
void calculateBalances();

//! \brief Returns the currencies.
std::vector<std::string> getCurrencies();

//! \brief Set Crypto prices, must be in the same order as getCurrencies().
void setPrice(const std::vector<double> &prices);

//! \brief Returns the static baseline EUR price (the read-only offline
//! fallback table) for the symbol; 0.0 for unknown symbols (never an
//! invented price).
//!
//! NOTE (dlsym surface contract): plain-C exported name, like the
//! series accessors below. Stateless table read - no DataHolder /
//! TransactionManager access, safe from any thread.
//! Do NOT move this declaration out of the extern "C" block: a
//! C++-linkage regression would silently re-mangle the export.
double getStaticPrice(const char *symbol);

//! \brief Returns the total money spent.
double getTotalMoneySpent();

//! \brief Returns the money spent per calendar month, oldest first:
//! "YYYY-MM;123.45" per month.
std::vector<std::string> getMoneySpentSeries();

//! \brief Returns the daily accounting series (spent/value/pl/bonus), see
//! TransactionManager::getDailySeries() for the line format.
std::vector<std::string> getDailySeries();

//! NOTE (dlsym surface contract): the three per-wallet / sub-daily series
//! accessors below are part of the C API. Declared in this extern "C"
//! block, the shared library exports them under their PLAIN names and the
//! CLI tester dlsyms exactly those plain names. Do NOT move their
//! declarations out of this block: a C++-linkage regression would silently
//! re-mangle the exports and break the dlsym consumer.

//! \brief Returns the per-wallet daily series (the daily wallet chart
//! source), see TransactionManager::getDailyWalletSeries() for the line
//! format.
std::vector<std::string> getDailyWalletSeries();

//! \brief Returns the money-spent series bucketed into fixed
//! [windowSeconds] slots, optionally starting at [fromEpoch] (epoch
//! seconds, 0 = unlimited); see TransactionManager::getSubDailySpentSeries()
//! for the line format and the window/opening-line contract.
std::vector<std::string> getSubDailySpentSeries(int windowSeconds, long long fromEpoch);

//! \brief Returns the per-wallet series in UNPRICED token amounts over the
//! same fixed slots; see TransactionManager::getSubDailyWalletSeries() for
//! the line format and the [fromEpoch] contract.
std::vector<std::string> getSubDailyWalletSeries(int windowSeconds, long long fromEpoch);

//! \brief Returns the total money spent on card.
double getTotalMoneySpentCard();

//! \brief Returns the total value of assets.
double getTotalValueOfAssets();

//! \brief Returns the total value of assets on card.
double getTotalValueOfAssetsCard();

//! \brief Returns the total bonus.
double getTotalBonus();

//! \brief Returns the total bonus on card.
double getTotalBonusCard();

//! \brief Returns the value of assets of the given wallet.
double getValueOfAssets(int walletId);

//! \brief Returns the bonus of the given wallet.
double getBonus(int walletId);

//! \brief Returns the money spent of the given wallet.
double getMoneySpent(int walletId);

//! \brief Returns the wallets formatted in XML.
std::vector<std::string> getWalletsAsStrings();

//! \brief Returns the card wallets formatted in XML.
std::vector<std::string> getCardWalletsAsStrings();

//! \brief Returns the wallet formatted in XML.
std::string getWalletAsString(int walletId);

//! \brief Returns the transaction formatted in XML.
std::vector<std::string> getTransactionsAsStrings();

//! \brief Returns the card transaction formatted in XML.
std::vector<std::string> getCardTransactionsAsStrings();

//! \brief Upserts one entry of the user-owned balance sheet from its
//! serde line (see BalanceSheet.h for the line contract: kind prefix +
//! ;-separated fields, the balance is core-computed and never taken from
//! the line). The core triggers its own save afterwards (atomic) when a
//! save dir is known, so the entry survives the next cold start.
//! false = line rejected (the sheet stays untouched) or no live core.
bool sheetSet(const std::string &line);

//! \brief Deletes the balance-sheet entry [kindId] (kind prefix + id,
//! e.g. "L<uuid>"); the core then auto-saves like sheetSet.
//! false = unknown entry or no live core.
bool sheetDelete(const std::string &kindId);

//! \brief The current balance-sheet entries of [kind] ("L") as lines, in
//! stored order; each line carries the core-computed current balance in
//! its trailing ;bal;<value> field. Empty for inactive kinds or no live
//! core.
std::vector<std::string> sheetList(const std::string &kind);

//! \brief The net-worth snapshot line(s) of the user-owned balance sheet
//! (see BalanceSheet::snapshot for the NW line contract): the core's own
//! current EUR wallet value (the internal EUR cash buffer INCLUDED - the
//! buffer-inclusion semantic is core-pinned here, the display cannot
//! drift from it) plus the declared asset sum, minus the current
//! liability balances, the net value core-computed. The caller (the UI)
//! shows the lines only. Empty when no live core.
std::vector<std::string> sheetSnapshot();

}

#endif //NF_TX_CORE_LIBRARY_H

#pragma clang diagnostic pop