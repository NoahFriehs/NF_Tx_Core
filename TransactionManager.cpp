
#include <set>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <ctime>
#include <unordered_set>
#include "TransactionManager.h"
#include "FileLog.h"
#include "BinaryUtil.h"


TransactionManager::TransactionManager() = default;

TransactionManager::TransactionManager(std::vector<BaseTransaction> &transactions) {
    std::lock_guard<std::mutex> lock(mutex);
    if (transactions.empty()) throw std::invalid_argument("Transactions is empty");

    this->transactions = transactions;
}

TransactionManager::~TransactionManager() = default;


thread_local long long TransactionManager::t_parseBudgetMs = 0;
thread_local std::chrono::steady_clock::time_point TransactionManager::t_parseDeadline{};
thread_local bool TransactionManager::t_lastParseCutByBudget = false;
thread_local int TransactionManager::t_lastParseUnprocessedRows = 0;

void TransactionManager::setParseBudgetMs(long long ms) {
    t_parseBudgetMs = ms;
    t_parseDeadline = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(std::max(0LL, ms));
    if (ms != 0) {
        // A positive budget (or the negative "already expired" test hook)
        // starts a fresh parse unit: the result state must describe THIS
        // parse, never a previous one. The release call (0) that ends a
        // parse keeps the flags readable until the caller has consumed
        // them.
        t_lastParseCutByBudget = false;
        t_lastParseUnprocessedRows = 0;
    }
}

bool TransactionManager::parseBudgetExceeded() {
    return t_parseBudgetMs != 0 && std::chrono::steady_clock::now() > t_parseDeadline;
}

void TransactionManager::markLastParseCut(int unprocessedRows) {
    t_lastParseCutByBudget = true;
    t_lastParseUnprocessedRows = unprocessedRows;
}

bool TransactionManager::wasLastParseCutByBudget() {
    return t_lastParseCutByBudget;
}

int TransactionManager::lastParseUnprocessedRows() {
    return t_lastParseUnprocessedRows;
}

void TransactionManager::processTransactions() {
    std::lock_guard<std::mutex> lock(mutex);
    getCurrenciesFromTxs();
    FileLog::i("TransactionManager", "Found " + std::to_string(currencies.size()) + " currencies");

    createWallets();

    addTransactionsToWallets();

    // Both cleanup passes take ~0.10 ms together, once per import under the
    // parse budget - there is no performance mode in the core to gate them on.
    removeEmptyWallets();
    removeUnusedTransactions();

    isReadyFlag = true;
    FileLog::i("TransactionManager", "Finished processing transactions");
}

void TransactionManager::getCurrenciesFromTxs() {
    if (!transactions.empty()) {
        hasTxData = true;
        for (auto &item: transactions) {
            if (std::find(currencies.begin(), currencies.end(), item.getCurrencyType()) ==
                currencies.end())
                currencies.push_back(item.getCurrencyType());
        }
    }
    if (!cardTransactions.empty()) {
        hasCardTxData = true;
        for (auto &item: cardTransactions) {
            if (std::find(cardTxTypes.begin(), cardTxTypes.end(),
                          item.getTransactionTypeString()) ==
                cardTxTypes.end())
                cardTxTypes.push_back(item.getTransactionTypeString());
        }
    }
}


bool TransactionManager::isReady() const {
    return isReadyFlag;
}

void TransactionManager::createWallets() {
    if (hasTxData) createCDCWallets();
    if (hasCardTxData) createCardWallets();
}

void TransactionManager::createCDCWallets() {
    for (auto &currency: currencies) {
        FileLog::i("TransactionManager", "Creating wallets for " + currency);
        // create wallets
        Wallet wallet(currency);
        Wallet outWallet(currency);
        outWallet.setIsOutWallet(true);
        // add wallet to map
        wallets.insert(std::pair<std::string, Wallet>(currency, wallet));
        outWallets.insert(std::pair<std::string, Wallet>(currency, outWallet));
    }
}

void TransactionManager::createCardWallets() {
    /*for (auto &txType: cardTxTypes) {
        FileLog::i("TransactionManager", "Creating wallets for " + txType);
        // create wallets
        Wallet wallet(txType);
        // add wallet to map
        cardWallets.insert(std::pair<std::string, Wallet>(txType, wallet));
    }*/
    Wallet wallet("EUR -> EUR");
    cardWallets.insert(std::pair<std::string, Wallet>("EUR -> EUR", wallet));
}

void TransactionManager::addTransactionsToWallets() {
    if (hasTxData) {
        if (currentMode == Mode::BlockPit) {
            addBlockPitTransactionsToWallets(0, transactions.size());
        } else {
            addCDCTransactionsToWallets(0, transactions.size());
        }
    }

    if (hasCardTxData) addCardTransactionsToWallets(0, cardTransactions.size());
}

void TransactionManager::addCardTransactionsToWallets(size_t begin, size_t count) {
    if (parseBudgetExceeded()) {
        FileLog::e("TransactionManager", "Parse budget exceeded: aborting wallet build");
        // Every row was parsed; the AGGREGATION stopped (the report keeps
        // the count at 0 - a row count would be a lie here).
        markLastParseCut(0);
        return;
    }
    // [begin, end): the rows assigned in THIS pass - the full bucket after
    // an init, only the freshly appended rows on the multi-file append (the
    // per-row assignment below is row-local; re-running it over existing rows
    // would credit their ledger deltas a second time).
    const size_t end = std::min(begin + count, cardTransactions.size());
    for (size_t i = begin; i < end; ++i) {
        auto &tx = cardTransactions[i];
        std::string tt = tx.getTransactionTypeString();
        if (tt == "EUR -> EUR") {
            getOrCreateWallet(cardWallets, "EUR -> EUR").addTransaction(tx, true);
            continue;
        }
        auto *wallet = getNonStrictWallet(tt);
        if (wallet == nullptr) continue;
        wallet->addTransaction(tx, true);
    }
}

void TransactionManager::addCDCTransactionsToWallets(size_t begin, size_t count) {
    if (parseBudgetExceeded()) {
        FileLog::e("TransactionManager", "Parse budget exceeded: aborting wallet build");
        // Every row was parsed; the AGGREGATION stopped (the report keeps
        // the count at 0 - a row count would be a lie here).
        markLastParseCut(0);
        return;
    }
    const size_t end = std::min(begin + count, transactions.size());
    for (size_t i = begin; i < end; ++i) {
        auto &tx = transactions[i];
        FileLog::v("TransactionManager", "Adding transaction to wallet: " + tx.getCurrencyType());
        // add transaction to wallet
        auto &walletRef = getOrCreateWallet(wallets, tx.getCurrencyType());
        auto *wallet = &walletRef;
        tx.setWalletId(wallet->getWalletId());
        tx.setFromWalletId(wallet->getWalletId());

        switch (tx.getTransactionType()) {
            case dust_conversion_credited:
            case crypto_purchase:
                wallet->addTransaction(tx, false);
                break;
            case supercharger_deposit:
            case crypto_earn_program_created:
            case lockup_lock:
            case supercharger_withdrawal:
            case crypto_earn_program_withdrawn:
            case rewards_platform_deposit_credited:
                break; // do nothing

            case supercharger_reward_to_app_credited:
            case crypto_earn_interest_paid:
            case referral_card_cashback:
            case reimbursement:
            case card_cashback_reverted:
            case admin_wallet_credited:
            case crypto_wallet_swap_credited:
                tx.setAmountToAmountBonus();
                wallet->addTransaction(tx, false);
                break;
            case crypto_wallet_swap_debited:
                tx.setAmountToAmountBonus();
                wallet->addTransaction(tx, false);
                break;
            case viban_purchase:
                vibianPurchase(tx);
                break;
            case crypto_withdrawal: {
                // The native books on the OUTSIDE wallet only (the withdraw
                // below): a withdrawal leaves the exchange, and counting its
                // fiat value on the inner wallet as well would double the
                // "money spent" card. The inner row keeps its balance flow
                // and list membership with a zeroed native so the row-based
                // spent series agree with the wallet scalar.
                BaseTransaction innerTx = tx;
                innerTx.setNativeAmount(0.0L);
                wallet->addTransaction(innerTx, false);
                getOrCreateWallet(outWallets, tx.getCurrencyType()).withdraw(tx);
                break;
            }
            case crypto_deposit: {
                // Incoming from the outside wallet: the same outside-only
                // native booking as crypto_withdrawal.
                BaseTransaction innerTx = tx;
                innerTx.setNativeAmount(0.0L);
                wallet->addTransaction(innerTx, false);
                getOrCreateWallet(outWallets, tx.getCurrencyType()).withdraw(tx);
                break;
            }
            case crypto_viban_exchange:
                wallet->withdraw(tx);
                getOrCreateWallet(wallets, "EUR").addTransaction(tx, false);
                break;
            case dust_conversion_debited:
                wallet->withdraw(tx);
                break;
            case STRING:
                FileLog::w("TransactionManager",
                           "Unknown transaction type: " + tx.getTransactionTypeString());
                break;
            case NONE:
                FileLog::e("TransactionManager", "Transaction type is NONE");
                throw std::invalid_argument("Transaction type is NONE");
        }

    }
}

void TransactionManager::addBlockPitTransactionsToWallets(size_t begin, size_t count) {
    if (parseBudgetExceeded()) {
        FileLog::e("TransactionManager", "Parse budget exceeded: aborting wallet build");
        // Every row was parsed; the AGGREGATION stopped (the report keeps
        // the count at 0 - a row count would be a lie here).
        markLastParseCut(0);
        return;
    }
    // Amounts are signed (see BaseTransaction::parseBlockPit): positive values
    // credit, negative values debit the wallet that owns the asset.
    const size_t end = std::min(begin + count, transactions.size());
    for (size_t i = begin; i < end; ++i) {
        auto &tx = transactions[i];
        // Splits a crypto-to-crypto swap into a credit on the incoming asset's
        // wallet and a matching debit on the outgoing asset's wallet.
        auto addSwap = [this](BaseTransaction &tx) {
            auto *inWallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
            auto *outWallet = &getOrCreateWallet(wallets, tx.getToCurrencyType());
            tx.setWalletId(inWallet->getWalletId());
            tx.setFromWalletId(outWallet->getWalletId());
            inWallet->addTransaction(tx, false);
            BaseTransaction debit = tx;   // stored copy for the outgoing wallet
            debit.setTransactionTypeString("Swap (debit)");
            debit.setCurrencyType(tx.getToCurrencyType());
            debit.setAmount(-tx.getToAmount());
            debit.setToAmount(0);
            outWallet->addTransaction(debit, false);   // sets debit.walletId
        };

        switch (tx.getTransactionType()) {
            case crypto_purchase: {
                // Fiat → crypto purchase (positive amount) or sale (negative
                // amount). The fiat side of the trade is the user's fiat
                // balance on the exchange, so it flows through the SAME
                // wallet map as the other fiat labels (Transfers In/Out,
                // Deposits, Withdrawals): purchases debit it, sales credit
                // it. nativeAmount stays 0 on the fiat row so "Money spent"
                // (summed over the inner wallets) still reflects only the
                // crypto wallets' purchase cost.
                auto *cryptoWallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
                tx.setWalletId(cryptoWallet->getWalletId());
                tx.setFromWalletId(cryptoWallet->getWalletId());
                cryptoWallet->addTransaction(tx, false);
                if (!tx.getToCurrencyType().empty() && tx.getToAmount() > 0) {
                    BaseTransaction fiatTx = tx;
                    fiatTx.setAmount(tx.getToAmount());
                    fiatTx.setNativeAmount(0.0L);
                    auto &fiatWallet = getOrCreateWallet(wallets, tx.getToCurrencyType());
                    if (tx.getAmount() >= 0) {
                        // Purchase: the fiat balance is spent
                        fiatWallet.withdraw(fiatTx);
                    } else {
                        // Sale: the fiat proceeds land in the balance
                        fiatWallet.addTransaction(fiatTx, false);
                    }
                }
                break;
            }

            case crypto_withdrawal: {
                // Signed negative amount: debits the asset wallet, credits outside
                auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
                tx.setWalletId(wallet->getWalletId());
                tx.setFromWalletId(wallet->getWalletId());
                wallet->addTransaction(tx, false);
                // Fiat rows: the inner fiat wallet IS the "outside" view — no
                // separate bookkeeping wallet (it only double-counted the row
                // in the list).
                if (!BaseTransaction::isFiatCurrency(tx.getCurrencyType())) {
                    getOrCreateWallet(outWallets, tx.getCurrencyType()).withdraw(tx);
                }
                break;
            }

            case crypto_deposit: {
                // Incoming: credits the asset wallet, debits outside
                auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
                tx.setWalletId(wallet->getWalletId());
                tx.setFromWalletId(wallet->getWalletId());
                wallet->addTransaction(tx, false);
                if (!BaseTransaction::isFiatCurrency(tx.getCurrencyType())) {
                    getOrCreateWallet(outWallets, tx.getCurrencyType()).withdraw(tx);
                }
                break;
            }

            case crypto_transfer: {
                // Non-Taxable In (credited) / Non-Taxable Out (debited) — the
                // sign in amount already encodes the direction.
                auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
                tx.setWalletId(wallet->getWalletId());
                tx.setFromWalletId(wallet->getWalletId());
                wallet->addTransaction(tx, false);
                break;
            }

            case crypto_earn_interest_paid: {
                // Interest/Staking: incoming credit
                auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
                tx.setWalletId(wallet->getWalletId());
                tx.setFromWalletId(wallet->getWalletId());
                wallet->addTransaction(tx, false);
                break;
            }

            case crypto_airdrop_credited:
            case crypto_bounty_credited: {
                // Rewarded into the incoming asset as bonus
                auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
                tx.setWalletId(wallet->getWalletId());
                tx.setFromWalletId(wallet->getWalletId());
                tx.setAmountToAmountBonus();
                wallet->addTransaction(tx, false);
                break;
            }

            case crypto_gift_received: {
                // A gift is a held position, not a reward: normal balance
                // credit. E.g. Kraken's 10,000 KFEE gift is spent on KFEE
                // fees, so the KFEE wallet has to balance.
                auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
                tx.setWalletId(wallet->getWalletId());
                tx.setFromWalletId(wallet->getWalletId());
                wallet->addTransaction(tx, false);
                break;
            }

            case STRING: {
                // Swaps and unknown labels. Every transaction must end up in a
                // wallet, otherwise removeEmptyWallets() leaves it dangling.
                if (!tx.getToCurrencyType().empty() && tx.getToAmount() > 0) {
                    addSwap(tx);
                } else if (!tx.getCurrencyType().empty()) {
                    auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());
                    tx.setWalletId(wallet->getWalletId());
                    tx.setFromWalletId(wallet->getWalletId());
                    wallet->addTransaction(tx, false);
                } else {
                    FileLog::w("TransactionManager",
                               "BlockPit line without usable asset: " + tx.getTransactionTypeString());
                }
                break;
            }

            case NONE:
                FileLog::e("TransactionManager", "Transaction type is NONE");
                throw std::invalid_argument("Transaction type is NONE");

            default:
                FileLog::w("TransactionManager",
                           "Unhandled BlockPit type: " + tx.getTransactionTypeString());
                break;
        }

        // Fees paid in a token that is not a side of the row (Kraken's KFEE
        // fee token) settle from that token's own balance; no fiat side of
        // the row is touched by such a fee.
        if (!tx.getFeeAsset().empty() && tx.getFeeAmount() > 0.0L &&
            tx.getFeeAsset() != tx.getCurrencyType() &&
            tx.getFeeAsset() != tx.getToCurrencyType()) {
            BaseTransaction feeTx = tx;
            feeTx.setTransactionTypeString("Fee");
            feeTx.setCurrencyType(tx.getFeeAsset());
            feeTx.setToAmount(0.0L);
            feeTx.setNativeAmount(0.0L);
            feeTx.setAmount(tx.getFeeAmount());
            auto &feeWallet = getOrCreateWallet(wallets, tx.getFeeAsset());
            feeTx.setWalletId(feeWallet.getWalletId());
            feeWallet.withdraw(feeTx);
        }
    }
}

void TransactionManager::vibianPurchase(BaseTransaction &tx) {
    auto *toWallet = &getOrCreateWallet(wallets, tx.getToCurrencyType());
    auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());

    tx.setWalletId(toWallet->getWalletId());
    tx.setFromWalletId(wallet->getWalletId());
    toWallet->addToTransaction(tx);
    wallet->addTransaction(tx);
}

void TransactionManager::removeEmptyWallets() {
    // A wallet whose own transaction list is empty is only removable when
    // no stored transaction references its id - transactions may carry a
    // walletId of a wallet they were never added to (e.g. special type
    // handling in addCDCTransactionsToWallets), and consumers of the data
    // (Room, the save format) require the referenced wallets to exist.
    // Wallet ids come from one forward-only counter, so they are unique
    // across all three maps and can be collected in a single set.
    std::unordered_set<int> referencedIds;
    for (const auto &tx: transactions) referencedIds.insert(tx.getWalletId());
    for (const auto &tx: cardTransactions) referencedIds.insert(tx.getWalletId());

    for (auto it = wallets.begin(); it != wallets.end();) {
        auto &wallet = it->second;
        if (wallet.getTransactions().empty() && !referencedIds.count(wallet.getWalletId())) {
            it = wallets.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = outWallets.begin(); it != outWallets.end();) {
        auto &wallet = it->second;
        if (wallet.getTransactions().empty() && !referencedIds.count(wallet.getWalletId())) {
            it = outWallets.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = cardWallets.begin(); it != cardWallets.end();) {
        auto &wallet = it->second;
        if (wallet.getTransactions().empty()
            && !referencedIds.count(wallet.getWalletId())) {
            it = cardWallets.erase(it);
        } else {
            ++it;
        }
    }
}

void TransactionManager::removeUnusedTransactions() {
    for (auto &tx: transactions) {
        if (tx.getWalletId() == -1) {
            FileLog::w("TransactionManager",
                       "Unused transaction: " + tx.getTransactionTypeString());
        }
    }

}

void TransactionManager::calculateWalletBalances() {

    //for each wallet add the currency Type to the vector if it is not already in there
    for (auto &wallet: wallets) {
        if (std::find(currencies.begin(), currencies.end(), wallet.second.getCurrencyType()) ==
            currencies.end())
            currencies.push_back(wallet.second.getCurrencyType());
    }

    walletBalanceMap.clear();
    cardWalletBalanceMap.clear();
    walletsBalance.reset();
    cardWalletsBalance.reset();
    checkTransactionManagerState();
    if (hasTxData)
        for (auto &walletRef: wallets) {
            auto walletBalance = std::make_unique<WalletBalance>();
            walletBalance->fillFromWallet(&walletRef.second);
            long double nativeBal = walletBalance->balance *
                    assetValue.getPrice(walletBalance->currencyType);
            walletBalance->nativeBalance = nativeBal;
            walletBalance->nativeBonusBalance =
                    assetValue.getPrice(walletBalance->currencyType) *
                    walletBalance->bonusBalance;
            walletBalanceMap.insert(
                    std::pair<std::string, WalletBalance>(walletRef.first, *walletBalance));
        }
    walletsBalance.fillFromWalletBalanceMap(walletBalanceMap);

    if (hasCardTxData)
        for (auto [txType, wallet]: cardWallets) {
            auto walletBalance = std::make_unique<WalletBalance>();
            walletBalance->fillFromWallet(&wallet);
            if (walletBalance->nativeBalance == 0 && walletBalance->balance != 0) {
                walletBalance->nativeBalance =
                        assetValue.getPrice(walletBalance->currencyType) * walletBalance->balance;
                walletBalance->nativeBonusBalance =
                        assetValue.getPrice(walletBalance->currencyType) *
                        walletBalance->bonusBalance;
            }
            cardWalletBalanceMap.insert(
                    std::pair<std::string, WalletBalance>(txType, *walletBalance));
        }
    cardWalletsBalance.fillFromWalletBalanceMap(cardWalletBalanceMap);

}

const std::vector<std::string> & TransactionManager::getCurrencies() {
    return currencies;
}

void TransactionManager::setPrices(const std::vector<double> &prices) {
    assetValue.loadCacheWithData(currencies, prices);
}

const std::vector<BaseTransaction> & TransactionManager::getTransactions() {
    return transactions;
}

double TransactionManager::getTotalMoneySpent() const {
    return walletsBalance.moneySpent;
}

std::vector<std::string> TransactionManager::getMoneySpentSeries() const {
    std::map<std::string, long double> months;
    for (const auto &entry: wallets) {
        // Same rule as WalletsBalance::fillFromWalletBalanceMap: the inner
        // fiat (EUR) wallet balances the fiat side of trades but is not
        // part of the "money spent" card total.
        if (entry.first == "EUR") continue;
        for (const auto &tx: entry.second.getTransactions()) {
            const auto &date = tx.getTransactionData().transactionDate;
            if (date.tm_year <= 0) continue;
            std::ostringstream key;
            key << (date.tm_year + 1900) << '-'
                << std::setw(2) << std::setfill('0') << (date.tm_mon + 1);
            months[key.str()] += tx.getNativeAmount();
        }
    }
    std::vector<std::string> out;
    out.reserve(months.size());
    for (const auto &month: months) {
        out.push_back(month.first + ";" + std::to_string(static_cast<double>(month.second)));
    }
    return out;
}

namespace {
//! Date key "YYYY-MM-DD", lexicographically chronological.
std::string dayKey(const std::tm &date) {
    std::ostringstream key;
    key << (date.tm_year + 1900) << '-'
        << std::setw(2) << std::setfill('0') << (date.tm_mon + 1) << '-'
        << std::setw(2) << (date.tm_mday);
    return key.str();
}

bool tmBefore(const std::tm &a, const std::tm &b) {
    if (a.tm_year != b.tm_year) return a.tm_year < b.tm_year;
    if (a.tm_mon != b.tm_mon) return a.tm_mon < b.tm_mon;
    if (a.tm_mday != b.tm_mday) return a.tm_mday < b.tm_mday;
    if (a.tm_hour != b.tm_hour) return a.tm_hour < b.tm_hour;
    if (a.tm_min != b.tm_min) return a.tm_min < b.tm_min;
    return a.tm_sec < b.tm_sec;
}

std::string dailyValue(double v) {
    return std::to_string(v);
}

//! One active day of the inner-wallet replay: the spent flow and the
//! per-currency balance/bonus deltas applied on that calendar day.
struct DayState {
    long double spentFlow = 0.0L;
    std::map<std::string, long double> balanceDelta;
    std::map<std::string, long double> bonusDelta;
};

//! Fixed-slot key for sub-daily series: "YYYY-MM-DD HH:MM" (UTC) of the
//! slot START, the epoch of the transaction floored to [windowSeconds].
//! The fields of [date] are UTC wall-clock parts (the CSV timestamps are
//! UTC; stringToTm fills them verbatim), so timegm is the exact inverse
//! of those parts - no local-timezone conversion anywhere.
//! "YYYY-MM-DD HH:MM" (UTC) of the window STARTING at [startEpoch].
//! Both key producers share this writer (the tx-time path floors the
//! transaction epoch, the fromEpoch path is grid-aligned by contract).
std::string windowKeyFromEpoch(long long startEpoch) {
    const time_t startSec = static_cast<time_t>(startEpoch);
    const struct tm startUtc = *gmtime(&startSec);
    std::ostringstream key;
    key << (startUtc.tm_year + 1900) << '-'
        << std::setw(2) << std::setfill('0') << (startUtc.tm_mon + 1) << '-'
        << std::setw(2) << (startUtc.tm_mday) << ' '
        << std::setw(2) << startUtc.tm_hour << ':'
        << std::setw(2) << startUtc.tm_min;
    return key.str();
}

std::string windowKey(const std::tm &date, int windowSeconds) {
    std::tm utc = date; // timegm must not mutate the caller's struct
    const time_t epoch = timegm(&utc);
    if (epoch < 0) return dayKey(date); // no usable time - fall back to the day
    const long long start = (static_cast<long long>(epoch) / windowSeconds) * windowSeconds;
    return windowKeyFromEpoch(start);
}

//! Replay the per-wallet ledgers of the INNER wallets (EUR excluded, the
//! same scope as WalletsBalance::fillFromWalletBalanceMap) chronologically
//! into one state per key (day or fixed slot). A std::map over the keys
//! iterates chronologically (both key formats sort time-ordered).
std::map<std::string, DayState> buildWindowState(
        const std::map<std::string, Wallet> &wallets,
        const std::function<std::string(const std::tm &)> &keyFn) {
    std::map<std::string, DayState> days;
    for (const auto &entry: wallets) {
        // Same scope as WalletsBalance::fillFromWalletBalanceMap (inner
        // fiat wallet excluded).
        if (entry.first == "EUR") continue;
        const std::vector<LedgerDelta> ledger = entry.second.getLedger();
        const auto &txs = entry.second.getTransactions();
        if (ledger.size() != txs.size()) {
            FileLog::w("TransactionManager:buildWindowState",
                       "Ledger out of sync for wallet " + entry.first +
                       " (" + std::to_string(ledger.size()) + " vs " +
                       std::to_string(txs.size()) + ")");
            continue;
        }
        // Stable sort by transaction date: the file order must not decide
        // the running balance within a bucket.
        std::vector<size_t> order(txs.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return tmBefore(txs[a].getTransactionData().transactionDate,
                            txs[b].getTransactionData().transactionDate);
        });
        for (size_t index: order) {
            const auto &date = txs[index].getTransactionData().transactionDate;
            if (date.tm_year <= 0) continue; // no usable date
            const auto &delta = ledger[index];
            DayState &day = days[keyFn(date)];
            day.spentFlow += delta.spent;
            day.balanceDelta[entry.first] += delta.balance;
            day.bonusDelta[entry.first] += delta.bonus;
        }
    }
    return days;
}

std::map<std::string, DayState> buildDailyState(
        const std::map<std::string, Wallet> &wallets) {
    return buildWindowState(wallets, &dayKey);
}

std::map<std::string, DayState> buildWindowState(
        const std::map<std::string, Wallet> &wallets, int windowSeconds) {
    return buildWindowState(wallets,
                            [windowSeconds](const std::tm &d) { return windowKey(d, windowSeconds); });
}

//! Epoch seconds of a "YYYY-MM-DD HH:MM" window key (UTC parts).
long long epochOfWindowKey(const std::string &key) {
    std::tm parts = {};
    std::istringstream ss(key);
    ss >> std::get_time(&parts, "%Y-%m-%d %H:%M");
    if (ss.fail()) return -1;
    return static_cast<long long>(timegm(&parts));
}
} // namespace

std::vector<std::string> TransactionManager::getDailySeries() const {
    const std::map<std::string, DayState> days = buildDailyState(wallets);

    // Carry the running totals over the days and emit the four series.
    std::vector<std::string> rows;
    std::map<std::string, long double> runningBalance;
    std::map<std::string, long double> runningBonus;
    long double spentSoFar = 0.0L;
    for (const auto &pair: days) {
        const std::string &key = pair.first;
        const DayState &day = pair.second;
        for (const auto &delta: day.balanceDelta) runningBalance[delta.first] += delta.second;
        for (const auto &delta: day.bonusDelta) runningBonus[delta.first] += delta.second;
        spentSoFar += day.spentFlow;

        double value = 0.0; // current prices, same source as the asset card
        for (const auto &bal: runningBalance)
            value += static_cast<double>(bal.second) * assetValue.getPrice(bal.first);
        double bonusValue = 0.0;
        for (const auto &bal: runningBonus)
            bonusValue += static_cast<double>(bal.second) * assetValue.getPrice(bal.first);

        rows.push_back("spent;" + key + ";" + dailyValue(static_cast<double>(day.spentFlow)));
        rows.push_back("value;" + key + ";" + dailyValue(value));
        rows.push_back("pl;" + key + ";"
                       + dailyValue(value - static_cast<double>(spentSoFar)));
        rows.push_back("bonus;" + key + ";" + dailyValue(bonusValue));
    }
    return rows;
}

std::vector<std::string> TransactionManager::getDailyWalletSeries() const {
    const std::map<std::string, DayState> days = buildDailyState(wallets);
    std::vector<std::string> rows;
    std::map<std::string, long double> runningBalance;
    std::map<std::string, long double> runningBonus;
    for (const auto &pair: days) {
        const DayState &day = pair.second;
        // Currencies touched on this day (std::set: stable per-day order)
        std::set<std::string> touched;
        for (const auto &d: day.balanceDelta) touched.insert(d.first);
        for (const auto &d: day.bonusDelta) touched.insert(d.first);
        for (const auto &cur: touched) {
            const long double balDelta = day.balanceDelta.count(cur)
                    ? day.balanceDelta.at(cur) : 0.0L;
            const long double bonusDelta = day.bonusDelta.count(cur)
                    ? day.bonusDelta.at(cur) : 0.0L;
            if (balDelta == 0.0L && bonusDelta == 0.0L) continue; // no change
            runningBalance[cur] += balDelta;
            runningBonus[cur] += bonusDelta;
            // Running TOKEN amounts (unpriced): the consumer values each
            // point with the prices valid at its time (the daily series
            // above is priced with the current prices only).
            rows.push_back(cur + ";" + pair.first + ";"
                            + dailyValue(static_cast<double>(runningBalance[cur])) + ";"
                            + dailyValue(static_cast<double>(runningBonus[cur])));
        }
    }
    return rows;
}

std::vector<std::string> TransactionManager::getSubDailySpentSeries(
        int windowSeconds, long long fromEpoch) const {
    if (windowSeconds <= 0) return {};
    const auto windows = buildWindowState(wallets, windowSeconds);
    std::vector<std::string> rows;
    for (const auto &pair: windows) {
        if (pair.second.spentFlow == 0.0L) continue; // no change
        if (fromEpoch > 0 && epochOfWindowKey(pair.first) < fromEpoch) continue;
        rows.push_back(pair.first + ";" + dailyValue(static_cast<double>(pair.second.spentFlow)));
    }
    return rows;
}

//! One line per currency with a non-zero running position: the state
//! BEFORE the cut slot's own deltas, at the fromEpoch slot key. A limited
//! window needs this opening, because the changed-slot emission carries
//! running values, not deltas - without the opening a position held before
//! the window would be unrecoverable (the chart frames end at "now" for
//! every export, fresh or stale). Zero positions are not carried.
static void emitSubDailyOpeningState(
        const std::map<std::string, long double> &runningBalance,
        const std::map<std::string, long double> &runningBonus,
        const std::string &key,
        std::vector<std::string> &rows) {
    std::set<std::string> held; // stable alphabetical order
    for (const auto &d: runningBalance) held.insert(d.first);
    for (const auto &d: runningBonus) held.insert(d.first);
    for (const auto &cur: held) {
        const long double balance = runningBalance.count(cur) ? runningBalance.at(cur) : 0.0L;
        const long double bonus = runningBonus.count(cur) ? runningBonus.at(cur) : 0.0L;
        if (balance == 0.0L && bonus == 0.0L) continue;
        rows.push_back(cur + ";" + key + ";"
                        + dailyValue(static_cast<double>(balance)) + ";"
                        + dailyValue(static_cast<double>(bonus)));
    }
}

std::vector<std::string> TransactionManager::getSubDailyWalletSeries(
        int windowSeconds, long long fromEpoch) const {
    if (windowSeconds <= 0) return {};
    const auto windows = buildWindowState(wallets, windowSeconds);
    std::vector<std::string> rows;
    std::map<std::string, long double> runningBalance;
    std::map<std::string, long double> runningBonus;
    bool openingEmitted = false;
    for (const auto &pair: windows) {
        // Skip emission (NOT accumulation): earlier slots still feed the
        // running totals, so the first EMITTED row carries the full state.
        const bool emit = fromEpoch <= 0 || epochOfWindowKey(pair.first) >= fromEpoch;
        if (!emit) {
            for (const auto &d: pair.second.balanceDelta) runningBalance[d.first] += d.second;
            for (const auto &d: pair.second.bonusDelta) runningBonus[d.first] += d.second;
            continue;
        }
        // The limited window opens with the state inherited from before the
        // cut, at the fromEpoch slot key itself (grid-aligned by contract -
        // the caller floors it), so a position carried in from before the
        // window stays visible even when no in-window slot matches it.
        if (fromEpoch > 0 && !openingEmitted) {
            openingEmitted = true;
            emitSubDailyOpeningState(runningBalance, runningBonus,
                                     windowKeyFromEpoch(fromEpoch), rows);
        }
        // Currencies touched in this slot (std::set: stable per-slot order)
        std::set<std::string> touched;
        for (const auto &d: pair.second.balanceDelta) touched.insert(d.first);
        for (const auto &d: pair.second.bonusDelta) touched.insert(d.first);
        for (const auto &cur: touched) {
            const long double balDelta = pair.second.balanceDelta.count(cur)
                    ? pair.second.balanceDelta.at(cur) : 0.0L;
            const long double bonusDelta = pair.second.bonusDelta.count(cur)
                    ? pair.second.bonusDelta.at(cur) : 0.0L;
            if (balDelta == 0.0L && bonusDelta == 0.0L) continue; // no change
            runningBalance[cur] += balDelta;
            runningBonus[cur] += bonusDelta;
            rows.push_back(cur + ";" + pair.first + ";"
                            + dailyValue(static_cast<double>(runningBalance[cur])) + ";"
                            + dailyValue(static_cast<double>(runningBonus[cur])));
        }
    }
    // Fully stale export: no slot at or after the cut, the opening state
    // stands alone (the caller carries it flat across the whole window).
    if (fromEpoch > 0 && !openingEmitted) {
        emitSubDailyOpeningState(runningBalance, runningBonus,
                                 windowKeyFromEpoch(fromEpoch), rows);
    }
    return rows;
}

double TransactionManager::getTotalValueOfAssets() const {
    return walletsBalance.nativeBalance;
}

double TransactionManager::getTotalBonus() const {
    return walletsBalance.nativeBonusBalance;
}

double TransactionManager::getValueOfAssets(int walletId) {
    for (const auto &item: walletBalanceMap) {
        if (item.second.walletId == walletId)
            return item.second.nativeBalance;
    }
    for (const auto &item: cardWalletBalanceMap) {
        if (item.second.walletId == walletId)
            return item.second.nativeBalance;
    }
    FileLog::w("TransactionsManager:getValueOfAssets",
               "No wallet found for id: " + std::to_string(walletId));
    return 0.0;
}

const std::map<std::string, Wallet> & TransactionManager::getWallets() {
    return wallets;
}

const std::map<std::string, Wallet> & TransactionManager::getOutWallets() {
    return outWallets;
}

double TransactionManager::getTotalBonus(int walletId) {
    for (const auto &item: walletBalanceMap) {
        if (item.second.walletId == walletId)
            return item.second.nativeBonusBalance;
    }
    for (const auto &item: cardWalletBalanceMap) {
        if (item.second.walletId == walletId)
            return item.second.nativeBonusBalance;
    }
    FileLog::w("TransactionsManager:getTotalBonus",
               "No wallet found for id: " + std::to_string(walletId));
    return 0.0;
}

double TransactionManager::getMoneySpent(int walletId) {
    for (const auto &item: walletBalanceMap) {
        if (item.second.walletId == walletId)
            return item.second.moneySpent;
    }
    for (const auto &item: cardWalletBalanceMap) {
        if (item.second.walletId == walletId)
            return item.second.moneySpent;
    }
    FileLog::w("TransactionsManager:getMoneySpent",
               "No wallet found for id: " + std::to_string(walletId));
    return 0.0;
}

void TransactionManager::setTransactions(std::vector<BaseTransaction> &transactions_, Mode mode) {
    std::lock_guard<std::mutex> lock(mutex);
    if (transactions_.empty()) throw std::invalid_argument("Transactions is empty");

    currentMode = mode;
    switch (mode) {
        case CDC:
        case Kraken:
        case BlockPit:
        case Default:
            transactions = transactions_;
            hasTxData = true;
            break;
        case Card:
        case CurveCard:
            cardTransactions = transactions_;
            hasCardTxData = true;
            break;
        case Custom:
            throw std::invalid_argument("Custom mode not implemented");
    }

    // The append contract's bookkeeping mirrors the bucket content: a
    // REPLACE rebuilds exactly this bucket's key set (the other bucket's
    // bookkeeping stays), so a later append dedups against what the manager
    // actually holds - and the single-file import path (a fresh manager) is
    // untouched, its rows are never filtered.
    if (mode == Mode::Card || mode == Mode::CurveCard) {
        appendCardKeys_.clear();
        registerAppendKeys(true, transactions_);
    } else {
        appendCryptoKeys_.clear();
        registerAppendKeys(false, transactions_);
    }
}

TransactionManager::AppendKey TransactionManager::appendKeyOf(const BaseTransaction &tx) {
    const TransactionData d = tx.getTransactionData();
    AppendKey k;
    const std::tm &t = d.transactionDate;
    k.time = (((((static_cast<long long>(t.tm_year) * 12) + t.tm_mon) * 31 + t.tm_mday) * 24
               + t.tm_hour) * 60 + t.tm_min) * 60 + t.tm_sec;
    k.description = d.description;
    const long double amount = tx.getAmount();
    k.amount = (amount != amount) ? 0.0L : amount;   // NaN guard: total ordering
    k.currency = tx.getCurrencyType();
    return k;
}

bool TransactionManager::hasAppendKey(bool isCard, const BaseTransaction &tx) const {
    const auto &keys = isCard ? appendCardKeys_ : appendCryptoKeys_;
    return keys.count(appendKeyOf(tx)) > 0;
}

void TransactionManager::registerAppendKeys(bool isCard, const std::vector<BaseTransaction> &txs) {
    auto &keys = isCard ? appendCardKeys_ : appendCryptoKeys_;
    for (const auto &tx: txs) keys.insert(appendKeyOf(tx));
}

size_t TransactionManager::appendTransactions(std::vector<BaseTransaction> &txs, Mode mode) {
    std::lock_guard<std::mutex> lock(mutex);
    if (txs.empty()) return 0;
    if (mode == Mode::Custom) throw std::invalid_argument("Custom mode not implemented");
    const bool card = (mode == Mode::Card || mode == Mode::CurveCard);
    auto &bucket = card ? cardTransactions : transactions;

    // Exact-tuple dedup at append time: a row identical to one already in
    // this bucket is a counted skip - never an invisible double account
    // (overlapping selections stay honest; exact only, never fuzzy).
    std::vector<BaseTransaction> fresh;
    size_t duplicates = 0;
    for (auto &tx: txs) {
        if (hasAppendKey(card, tx)) {
            ++duplicates;
            continue;
        }
        fresh.push_back(tx);
    }
    appendedDuplicateSkips_ += duplicates;
    if (fresh.empty()) {
        FileLog::i("TransactionManager",
                   "Append: all " + std::to_string(duplicates) +
                   " row(s) are duplicates of existing rows, nothing added");
        return 0;
    }

    const size_t begin = bucket.size();
    bucket.insert(bucket.end(), fresh.begin(), fresh.end());
    if (card) {
        hasCardTxData = true;
    } else {
        hasTxData = true;
    }
    registerAppendKeys(card, fresh);

    // Incremental wallet build over the NEW rows only (see the range
    // comment on addCardTransactionsToWallets): getCurrenciesFromTxs and
    // createWallets are additive/idempotent, removeEmptyWallets only removes
    // wallets that are still empty and unreferenced.
    getCurrenciesFromTxs();
    createWallets();
    if (card) {
        addCardTransactionsToWallets(begin, fresh.size());
    } else if (mode == Mode::BlockPit) {
        addBlockPitTransactionsToWallets(begin, fresh.size());
    } else {
        addCDCTransactionsToWallets(begin, fresh.size());
    }
    removeEmptyWallets();
    FileLog::i("TransactionManager", "Append: added " + std::to_string(fresh.size()) +
             " row(s), skipped " + std::to_string(duplicates) + " duplicate(s)");
    return fresh.size();
}

std::string TransactionManager::checkCardTxTypes(const std::string &tt, const std::string &txType) {
    auto it = std::find(cardTxTypes.begin(), cardTxTypes.end(), tt);
    if (it != cardTxTypes.end()) {
        cardTxTypes.erase(it);
    }
    cardTxTypes.push_back(txType);
    return txType;
}

std::string TransactionManager::checkForRefund(std::string &tt) {
    if (tt.find("Refund: ") != std::string::npos) {
        tt = checkCardTxTypes(tt, tt.substr(8));
    }
    if (tt.find("Refund reversal: ") != std::string::npos) {
        tt = checkCardTxTypes(tt, tt.substr(17));
    }
    return tt;
}

Wallet *TransactionManager::getNonStrictWallet(std::string &tt) {
    std::string modifiedTT = checkForRefund(tt);
    modifiedTT = removePrefix(modifiedTT, "Crv*");

    for (auto &[name, w]: cardWallets) {
        size_t spacePos = modifiedTT.find_first_of(' ');

        if (spacePos != std::string::npos) {
            std::string prefix = modifiedTT.substr(0, spacePos);
            if (w.getCurrencyType().find(prefix) != std::string::npos) {
                w.setCurrencyType(prefix);
                checkCardTxTypes(modifiedTT, prefix);
                return &w;
            }
        } else if (w.getCurrencyType().find(modifiedTT) != std::string::npos) {
            return &w;
        }
    }

    //if no Wallet found, create new one
    modifiedTT = checkCardTxTypes(tt, modifiedTT);
    Wallet wallet(modifiedTT);
    cardWallets.insert(std::pair<std::string, Wallet>(modifiedTT, wallet));
    return &getOrCreateWallet(cardWallets, modifiedTT);
}

Wallet &TransactionManager::getOrCreateWallet(std::map<std::string, Wallet> &target,
                                              const std::string &key) {
    auto it = target.find(key);
    if (it != target.end()) return it->second;
    return target.emplace(key, Wallet(key)).first->second;
}

const std::map<std::string, Wallet> & TransactionManager::getCardWallets() {
    return cardWallets;
}

const std::vector<BaseTransaction> & TransactionManager::getCardTransactions() {
    return cardTransactions;
}

double TransactionManager::getTotalValueOfAssetsCard() const {
    return cardWalletsBalance.nativeBalance;
}

double TransactionManager::getTotalBonusCard() const {
    return cardWalletsBalance.nativeBonusBalance;
}

double TransactionManager::getTotalMoneySpentCard() const {
    return cardWalletsBalance.moneySpent;
}

Wallet *TransactionManager::getWallet(int walletId) {
    if (hasTxData)
        for (auto &[txType, wallet]: wallets) {
            if (wallet.getWalletId() == walletId)
                return &wallet;
        }
    if (hasCardTxData)
        for (auto &[txType, wallet]: cardWallets) {
            if (wallet.getWalletId() == walletId)
                return &wallet;
        }
    FileLog::e("TransactionsManager:getWallet",
               "No wallet found for id: " + std::to_string(walletId));
    return nullptr;
}


bool TransactionManager::saveData(const std::string &dirPath) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Saving data to dir: " + dirPath);
    saveDir_ = dirPath;   // the sheet auto-save target of later mutations

    std::vector<WalletStruct> walletStructVector;
    for (auto &[name, wallet]: wallets) {
        walletStructVector.push_back(*wallet.getWalletStruct());
    }
    for (auto &[name, wallet]: outWallets) {
        walletStructVector.push_back(*wallet.getWalletStruct());
    }
    std::vector<WalletStruct> cardWalletStructVector;
    for (auto &[name, wallet]: cardWallets) {
        cardWalletStructVector.push_back(*wallet.getWalletStruct());
    }

    const auto state = getTransactionManagerState();

    // The save layout writes <dirPath> + "wallets" | "cardWallets" | "state",
    // so the directory (with a trailing slash) must exist. Creating it here
    // (instead of relying on the caller) keeps every platform - including
    // Android, where the directory is never created otherwise - working.
    std::error_code ec;
    std::filesystem::create_directories(dirPath, ec);
    if (ec) {
        FileLog::e("TransactionManager", "Could not create save dir " + dirPath + ": " + ec.message());
        return false;
    }

    const bool ok = BinaryUtil::writeWalletStore(dirPath + "wallets", walletStructVector) &&
                    BinaryUtil::writeWalletStore(dirPath + "cardWallets", cardWalletStructVector) &&
                    BinaryUtil::writeStateFile(dirPath + "state", state);
    if (!ok) {
        FileLog::e("TransactionManager", "Saving data failed");
        return false;
    }
    if (upgradedLegacy_) {
        FileLog::i("TransactionManager",
                   "Legacy save files upgraded to format v" +
                           std::to_string(BinaryUtil::kVersionV5));
        upgradedLegacy_ = false;
    }
    FileLog::i("TransactionManager", "Finished saving data");
    return true;
}

void TransactionManager::loadData(const std::string &dirPath) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Loading data from dir: " + dirPath);
    saveDir_ = dirPath;   // a loaded store becomes the auto-save target
    clearAll();           // resets import state; the sheet transport survives it
    upgradedLegacy_ = false;

    std::vector<WalletStruct> walletStructVector;
    std::vector<WalletStruct> cardWalletStructVector;
    TransactionManagerState state;

    // v3/v4 files are read as-is (the record layout follows each file's
    // own version byte); legacy v2 files (same long double ABI) are
    // converted, and any pre-v4 store is re-saved in v4 on the next store.
    const int stateVersion = BinaryUtil::readStateFile(dirPath + "state", state);
    const int walletVersion = BinaryUtil::readWalletStore(dirPath + "wallets", walletStructVector);
    const int cardVersion = BinaryUtil::readWalletStore(dirPath + "cardWallets", cardWalletStructVector);

    // A healthy store has ALL THREE files read clean (saveData writes the
    // set atomically). Any other combination - a file missing, or a
    // MID-RECORD failure (the reader returns 0 + an error log) - is an
    // inconsistent store: treat it as absent instead of loading a
    // half-state. The app's guarded re-import is the recovery (Room
    // fallback + the fast-start re-parse).
    if (stateVersion == 0 || walletVersion == 0 || cardVersion == 0) {
        if (stateVersion > 0 || walletVersion > 0 || cardVersion > 0) {
            FileLog::e("TransactionManager",
                       "Save store inconsistent (versions state/wallets/cardWallets: " +
                       std::to_string(stateVersion) + "/" + std::to_string(walletVersion) +
                       "/" + std::to_string(cardVersion) + "); treating the store as absent");
        }
        setTransactionManagerState(TransactionManagerState());
        FileLog::i("TransactionManager", "No usable save store; continuing with empty state");
        return;
    }

    upgradedLegacy_ = stateVersion < BinaryUtil::kVersionV5 ||
                      walletVersion < BinaryUtil::kVersionV5 ||
                      cardVersion < BinaryUtil::kVersionV5;

    setTransactionManagerState(state);

    if (hasTxData)
        for (auto &walletStruct: walletStructVector) {
            Wallet wallet;
            wallet.setWalletData(walletStruct);
            if (!wallet.getIsOutWallet())
                wallets.insert(std::pair<std::string, Wallet>(walletStruct.currencyType, wallet));
            else
                outWallets.insert(
                        std::pair<std::string, Wallet>(walletStruct.currencyType, wallet));

            auto txs = wallet.getTransactions();
            transactions.insert(transactions.end(), txs.begin(), txs.end());
        }

    if (hasCardTxData)
        for (auto &walletStruct: cardWalletStructVector) {
            Wallet wallet;
            wallet.setWalletData(walletStruct);
            cardWallets.insert(std::pair<std::string, Wallet>(walletStruct.currencyType, wallet));
            auto txs = wallet.getTransactions();
            cardTransactions.insert(cardTransactions.end(), txs.begin(), txs.end());
        }

    // Multi-file append contract: the restored rows ARE the seen keys, so an
    // append after a restore dedups against the restored content (this also
    // covers the v2 -> v3 upgrade path, which goes through the same load).
    registerAppendKeys(false, transactions);
    registerAppendKeys(true, cardTransactions);

    FileLog::i("TransactionManager", "Finished loading data");
}

TransactionManagerState TransactionManager::getTransactionManagerState() {
    TransactionManagerState state;
    state.hasCardTxData = hasCardTxData;
    state.hasTxData = hasTxData;
    state.isReadyFlag = isReadyFlag;
    state.txIdCounter = BaseTransaction::getTxIdCounter();
    state.walletIdCounter = Wallet::getWalletIdCounter();
    state.currencies = currencies;
    state.cardTxTypes = cardTxTypes;
    state.balanceSheetText = balanceSheetText_;
    return state;
}

void TransactionManager::setBalanceSheetText(const std::string &text) {
    std::lock_guard<std::mutex> lock(mutex);
    balanceSheetText_ = text;
}

std::string TransactionManager::getBalanceSheetText() const {
    std::lock_guard<std::mutex> lock(mutex);
    return balanceSheetText_;
}

std::string TransactionManager::getSaveDir() const {
    std::lock_guard<std::mutex> lock(mutex);
    return saveDir_;
}

void TransactionManager::setTransactionManagerState(const TransactionManagerState &state) {
    BaseTransaction::setTxIdCounter(state.txIdCounter);
    Wallet::setWalletIdCounter(state.walletIdCounter);
    hasCardTxData = state.hasCardTxData;
    hasTxData = state.hasTxData;
    isReadyFlag = state.isReadyFlag;
    currencies = state.currencies;
    cardTxTypes = state.cardTxTypes;
    balanceSheetText_ = state.balanceSheetText;
}

bool TransactionManager::checkSavedData(const std::string &dirPath) {
    std::lock_guard<std::mutex> lock(mutex);
    // Must use the same dirPath prefix that saveData/loadData write to.
    return checkIfFileExists(dirPath + "wallets") &&
           checkIfFileExists(dirPath + "cardWallets") &&
           checkIfFileExists(dirPath + "state");
}

bool TransactionManager::checkIfFileExists(const std::string &file) {
    std::ifstream f(file);
    return f.good();
}

void TransactionManager::clearAll() {
    FileLog::i("TransactionManager", "Clearing all data");
    // Deliberately NOT touched here: balanceSheetText_ (the user-owned
    // sheet - the restore sequence runs through this reset and must not
    // wipe it) and saveDir_ (the auto-save target of the live store).
    hasTxData = false;
    hasCardTxData = false;

    transactions.clear();
    cardTransactions.clear();
    appendCryptoKeys_.clear();
    appendCardKeys_.clear();
    appendedDuplicateSkips_ = 0;
    wallets.clear();
    outWallets.clear();
    cardWallets.clear();
    currencies.clear();
    cardTxTypes.clear();
    isReadyFlag = false;
    walletBalanceMap.clear();
    cardWalletBalanceMap.clear();
    walletsBalance.reset();
    cardWalletsBalance.reset();

    FileLog::i("TransactionManager", "Cleared all data");
}

void TransactionManager::setWalletData(const std::vector<WalletData> &_wallets) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Setting wallet data");
    for (auto &walletData: _wallets) {
        Wallet wallet;
        wallet.setWalletData(walletData.getWalletStruct());
        if (!wallet.getIsOutWallet())
            wallets.insert(std::pair<std::string, Wallet>(walletData.currencyType, wallet));
        else outWallets.insert(std::pair<std::string, Wallet>(walletData.currencyType, wallet));

        auto txs = wallet.getTransactions();
        transactions.insert(transactions.end(), txs.begin(), txs.end());
    }
}

void TransactionManager::setCardWalletData(const std::vector<WalletData> &_cardWallets) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Setting card wallet data");
    for (auto &walletData: _cardWallets) {
        Wallet wallet;
        wallet.setWalletData(walletData.getWalletStruct());
        cardWallets.insert(std::pair<std::string, Wallet>(walletData.currencyType, wallet));
        auto txs = wallet.getTransactions();
        cardTransactions.insert(cardTransactions.end(), txs.begin(), txs.end());
    }
}

void TransactionManager::setTransactionData(const std::vector<TransactionData> &txData) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Setting transaction data");
    for (auto &tx: txData) {
        BaseTransaction transaction;
        transaction.setTransactionData(tx.getTransactionStruct());
        transactions.push_back(transaction);
    }
}

void TransactionManager::setCardTransactionData(const std::vector<TransactionData> &txData) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Setting card transaction data");
    for (auto &tx: txData) {
        BaseTransaction transaction;
        transaction.setTransactionData(tx.getTransactionStruct());
        cardTransactions.push_back(transaction);
    }
}

// Unlocked helper: DataHolder serializes all TransactionManager access,
// and the TM mutex is already held by several TM methods, so locking
// again here would deadlock.
void TransactionManager::checkTransactionManagerState() {
    if (!wallets.empty() || !outWallets.empty() || !transactions.empty()) {
        hasTxData = true;
    }
    if (!cardWallets.empty() || !cardTransactions.empty()) {
        hasCardTxData = true;
    }
}

Wallet *TransactionManager::getCardWallet(int walletId) {
    if (hasCardTxData)
        for (auto &[txType, wallet]: cardWallets) {
            if (wallet.getWalletId() == walletId)
                return &wallet;
        }
    FileLog::e("TransactionsManager", "No card wallet found for id: " + std::to_string(walletId));
    return nullptr;
}

//! Returns the number of active modes (1 = Crypto, 2 = Card, 3 = Crypto + Card)
int TransactionManager::getActiveModes() const {
    int activeModes = 0;
    if (hasTxData) activeModes++;
    if (hasCardTxData) activeModes += 2;
    return activeModes;
}
