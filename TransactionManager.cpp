
#include <set>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <sstream>
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

void TransactionManager::setParseBudgetMs(long long ms) {
    t_parseBudgetMs = ms;
    t_parseDeadline = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(std::max(0LL, ms));
}

bool TransactionManager::parseBudgetExceeded() {
    return t_parseBudgetMs != 0 && std::chrono::steady_clock::now() > t_parseDeadline;
}

void TransactionManager::processTransactions() {
    std::lock_guard<std::mutex> lock(mutex);
    getCurrenciesFromTxs();
    FileLog::i("TransactionManager", "Found " + std::to_string(currencies.size()) + " currencies");

    createWallets();

    addTransactionsToWallets();

    removeEmptyWallets(); //TODO: do this not in performance mode

    removeUnusedTransactions(); //TODO: do this not in performance mode (both together take 0.10ms)

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
            addBlockPitTransactionsToWallets();
        } else {
            addCDCTransactionsToWallets();
        }
    }

    if (hasCardTxData) addCardTransactionsToWallets();
}

void TransactionManager::addCardTransactionsToWallets() {
    if (parseBudgetExceeded()) {
        FileLog::e("TransactionManager", "Parse budget exceeded: aborting wallet build");
        return;
    }
    for (auto &tx: cardTransactions) {
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

void TransactionManager::addCDCTransactionsToWallets() {
    if (parseBudgetExceeded()) {
        FileLog::e("TransactionManager", "Parse budget exceeded: aborting wallet build");
        return;
    }
    for (auto &tx: transactions) {
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
            case crypto_withdrawal:
                wallet->addTransaction(tx, false);
                getOrCreateWallet(outWallets, tx.getCurrencyType()).withdraw(tx);
                break;
            case crypto_deposit:    //TODO: check if this is correct with the new data, we have no Tx for this until now
                wallet->addTransaction(tx, false);
                getOrCreateWallet(outWallets, tx.getCurrencyType()).withdraw(tx);
                break;
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

void TransactionManager::addBlockPitTransactionsToWallets() {
    if (parseBudgetExceeded()) {
        FileLog::e("TransactionManager", "Parse budget exceeded: aborting wallet build");
        return;
    }
    // Amounts are signed (see BaseTransaction::parseBlockPit): positive values
    // credit, negative values debit the wallet that owns the asset.
    for (auto &tx: transactions) {
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

//! Replay the per-wallet ledgers of the INNER wallets (EUR excluded, the
//! same scope as WalletsBalance::fillFromWalletBalanceMap) chronologically
//! into one state per active day. A std::map over the date keys iterates
//! chronologically.
std::map<std::string, DayState> buildDailyState(
        const std::map<std::string, Wallet> &wallets) {
    std::map<std::string, DayState> days;
    for (const auto &entry: wallets) {
        // Same scope as WalletsBalance::fillFromWalletBalanceMap (inner
        // fiat wallet excluded).
        if (entry.first == "EUR") continue;
        const std::vector<LedgerDelta> ledger = entry.second.getLedger();
        const auto &txs = entry.second.getTransactions();
        if (ledger.size() != txs.size()) {
            FileLog::w("TransactionManager:buildDailyState",
                       "Ledger out of sync for wallet " + entry.first +
                       " (" + std::to_string(ledger.size()) + " vs " +
                       std::to_string(txs.size()) + ")");
            continue;
        }
        // Stable sort by transaction date: the file order must not decide
        // the running balance within a day.
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
            DayState &day = days[dayKey(date)];
            day.spentFlow += delta.spent;
            day.balanceDelta[entry.first] += delta.balance;
            day.bonusDelta[entry.first] += delta.bonus;
        }
    }
    return days;
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
            cardTransactions = transactions_;
            hasCardTxData = true;
            break;
        case Custom:
            throw std::invalid_argument("Custom mode not implemented");
    }
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


void TransactionManager::saveData(const std::string &dirPath) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Saving data to dir: " + dirPath);

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

    if (!BinaryUtil::writeWalletStore(dirPath + "wallets", walletStructVector) ||
        !BinaryUtil::writeWalletStore(dirPath + "cardWallets", cardWalletStructVector) ||
        !BinaryUtil::writeStateFile(dirPath + "state", state)) {
        FileLog::e("TransactionManager", "Saving data failed");
        return;
    }
    if (upgradedLegacy_) {
        FileLog::i("TransactionManager", "Legacy v2 save files upgraded to format v3");
        upgradedLegacy_ = false;
    }
    FileLog::i("TransactionManager", "Finished saving data");
}

void TransactionManager::loadData(const std::string &dirPath) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Loading data from dir: " + dirPath);
    clearAll();
    upgradedLegacy_ = false;

    std::vector<WalletStruct> walletStructVector;
    std::vector<WalletStruct> cardWalletStructVector;
    TransactionManagerState state;

    // v3 files are read as-is; legacy v2 files (same long double ABI) are
    // converted, and re-saved in v3 on the next store.
    const int stateVersion = BinaryUtil::readStateFile(dirPath + "state", state);
    const int walletVersion = BinaryUtil::readWalletStore(dirPath + "wallets", walletStructVector);
    const int cardVersion = BinaryUtil::readWalletStore(dirPath + "cardWallets", cardWalletStructVector);
    upgradedLegacy_ = stateVersion == BinaryUtil::kVersionV2 ||
                      walletVersion == BinaryUtil::kVersionV2 ||
                      cardVersion == BinaryUtil::kVersionV2;

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
    return state;
}

void TransactionManager::setTransactionManagerState(const TransactionManagerState &state) {
    BaseTransaction::setTxIdCounter(state.txIdCounter);
    Wallet::setWalletIdCounter(state.walletIdCounter);
    hasCardTxData = state.hasCardTxData;
    hasTxData = state.hasTxData;
    isReadyFlag = state.isReadyFlag;
    currencies = state.currencies;
    cardTxTypes = state.cardTxTypes;
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
    hasTxData = false;
    hasCardTxData = false;

    transactions.clear();
    cardTransactions.clear();
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
