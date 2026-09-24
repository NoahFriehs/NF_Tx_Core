
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include "TransactionManager.h"
#include "FileLog.h"
#include "BinaryUtil.h"
#include "TransactionManager/TMState.h"
#include "Util/CharUtil.h"

using BinaryUtil::deserializeStruct;
using BinaryUtil::deserializeVector;
using BinaryUtil::serializeStruct;
using BinaryUtil::serializeVector;


TransactionManager::TransactionManager() = default;

TransactionManager::TransactionManager(std::vector<BaseTransaction> &transactions) {
    std::lock_guard<std::mutex> lock(mutex);
    if (transactions.empty()) throw std::invalid_argument("Transactions is empty");

    this->transactions = transactions;
}

TransactionManager::~TransactionManager() = default;


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
    if (hasTxData) addCDCTransactionsToWallets();

    if (hasCardTxData) addCardTransactionsToWallets();
}

void TransactionManager::addCardTransactionsToWallets() {
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

void TransactionManager::vibianPurchase(BaseTransaction &tx) {
    auto *toWallet = &getOrCreateWallet(wallets, tx.getToCurrencyType());
    auto *wallet = &getOrCreateWallet(wallets, tx.getCurrencyType());

    tx.setWalletId(toWallet->getWalletId());
    tx.setFromWalletId(wallet->getWalletId());
    toWallet->addToTransaction(tx);
    wallet->addTransaction(tx);
}

void TransactionManager::removeEmptyWallets() {
    for (auto it = wallets.begin(); it != wallets.end();) {
        if (it->second.getTransactions().empty()) {
            it = wallets.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = outWallets.begin(); it != outWallets.end();) {
        if (it->second.getTransactions().empty()) {
            it = outWallets.erase(it);
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
        for (auto &wallet: wallets) {

            //if (wallet.second.getCurrencyType() == "EUR") continue;

            auto walletBalance = std::make_unique<WalletBalance>();
            walletBalance->fillFromWallet(&wallet.second);
            if (walletBalance->nativeBalance == 0 && walletBalance->balance != 0 || true) {
                walletBalance->nativeBalance =
                        assetValue.getPrice(walletBalance->currencyType) * walletBalance->balance;
                walletBalance->nativeBonusBalance =
                        assetValue.getPrice(walletBalance->currencyType) *
                        walletBalance->bonusBalance;
            }
            walletBalanceMap.insert(
                    std::pair<std::string, WalletBalance>(wallet.first, *walletBalance));
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

    switch (mode) {
        case CDC:
        case Kraken:
            transactions = transactions_;
            hasTxData = true;
            break;
        case Card:
            cardTransactions = transactions_;
            hasCardTxData = true;
            break;
        case Custom:
            throw std::invalid_argument("Custom mode not implemented");
        case Default:
            transactions = transactions_;
            hasTxData = true;
            break;
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
    FileLog::i("TransactionManager", "Saving data");
    std::vector<WalletStruct> walletStructVector;
    std::vector<WalletStruct> cardWalletStructVector;
    std::vector<CWalletStruct> cWalletStructVector;
    std::vector<CWalletStruct> cCardWalletStructVector;

    for (auto &[name, wallet]: wallets) {
        walletStructVector.push_back(*wallet.getWalletStruct());
    }
    for (auto &[name, wallet]: outWallets) {
        walletStructVector.push_back(*wallet.getWalletStruct());
    }
    for (auto &[name, wallet]: cardWallets) {
        cardWalletStructVector.push_back(*wallet.getWalletStruct());
    }

    auto state = getTransactionManagerState();

    FileLog::i("TransactionManager", "Saving data to dir: " + dirPath);

    size_t walletsSize = walletStructVector.size();
    size_t cardWalletsSize = cardWalletStructVector.size();

    for (size_t i = 0; i < walletsSize; i++) {
        cWalletStructVector.push_back(CWalletStruct::convertToCWalletStruct(walletStructVector[i]));
    }
    for (size_t i = 0; i < cardWalletsSize; i++) {
        cCardWalletStructVector.push_back(
                CWalletStruct::convertToCWalletStruct(cardWalletStructVector[i]));
    }

    serializeVector(cWalletStructVector, dirPath + "wallets");
    serializeVector(cCardWalletStructVector, dirPath + "cardWallets");
    // Note: state.currencies/cardTxTypes are truncated to MAX_WALLETS entries
    // (see getTransactionManagerState), so "big" states are persisted with a
    // limited currency list. The wallet data itself is unaffected.
    if (state.isBig) {
        FileLog::w("TransactionManager",
                   "State is 'big'; currencies/card transaction types are truncated "
                           "to " + std::to_string(MAX_WALLETS) + " entries when saving");
    }
    serializeStruct(state, dirPath + "state");

    FileLog::i("TransactionManager", "Finished saving data");
}

void TransactionManager::loadData(const std::string &dirPath) {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Loading data");
    clearAll();
    std::vector<CWalletStruct> walletStructVector;
    std::vector<CWalletStruct> cardWalletStructVector;
    TransactionManagerState state;

    deserializeStruct(state, dirPath + "state");

    deserializeVector(walletStructVector, dirPath + "wallets");
    deserializeVector(cardWalletStructVector, dirPath + "cardWallets");

    FileLog::i("TransactionManager", "Loading data from file");

    setTransactionManagerState(state);

    if (hasTxData)
        for (auto &walletStruct: walletStructVector) {
            Wallet wallet;
            wallet.setWalletData(CWalletStruct::convertToWalletStruct(walletStruct));
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
            wallet.setWalletData(CWalletStruct::convertToWalletStruct(walletStruct));
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
    state.isBig = currencies.size() > MAX_WALLETS || cardTxTypes.size() > MAX_WALLETS;
    state.txIdCounter = BaseTransaction::getTxIdCounter();
    state.walletIdCounter = Wallet::getWalletIdCounter();

    // Bounded copies into the fixed-size state arrays; live members are
    // never modified here.
    if (currencies.size() > MAX_WALLETS) {
        FileLog::w("TransactionManager",
                   "Truncating " + std::to_string(currencies.size() - MAX_WALLETS)
                           + " currencies for state export");
    }
    size_t currencyCount = std::min(currencies.size(),
                                    static_cast<size_t>(MAX_WALLETS));
    for (size_t i = 0; i < currencyCount; i++) {
        if (currencies[i].length() >= MAX_STRING_LENGTH) {
            FileLog::w("TransactionManager",
                       "Currency name truncated: " + currencies[i]);
        }
        stringToCharArray(state.currencies[i], sizeof(state.currencies[i]), currencies[i]);
    }

    if (cardTxTypes.size() > MAX_WALLETS) {
        FileLog::w("TransactionManager",
                   "Truncating " + std::to_string(cardTxTypes.size() - MAX_WALLETS)
                           + " card transaction types for state export");
    }
    size_t typeCount = std::min(cardTxTypes.size(), static_cast<size_t>(MAX_WALLETS));
    for (size_t i = 0; i < typeCount; i++) {
        if (cardTxTypes[i].length() >= MAX_STRING_LENGTH) {
            FileLog::w("TransactionManager",
                       "Card tx type truncated: " + cardTxTypes[i]);
        }
        stringToCharArray(state.cardTxTypes[i], sizeof(state.cardTxTypes[i]), cardTxTypes[i]);
    }

    return state;
}

void TransactionManager::setTransactionManagerState(const TransactionManagerState &state) {
    BaseTransaction::setTxIdCounter(state.txIdCounter);
    Wallet::setWalletIdCounter(state.walletIdCounter);
    hasCardTxData = state.hasCardTxData;
    hasTxData = state.hasTxData;
    // Clear before restoring: setTransactionManagerState must be idempotent
    // even if called twice (e.g. double init).
    currencies.clear();
    cardTxTypes.clear();
    for (const auto &currency: state.currencies) {
        if (currency[0] == '\0') break;
        currencies.emplace_back(currency);
    }
    for (const auto &type: state.cardTxTypes) {
        if (type[0] == '\0') break;
        cardTxTypes.emplace_back(type);
    }
    isReadyFlag = state.isReadyFlag;
}

bool TransactionManager::checkSavedData() {
    std::lock_guard<std::mutex> lock(mutex);
    FileLog::i("TransactionManager", "Checking saved data");

    return checkIfFileExists("wallets") && checkIfFileExists("cardWallets") &&
           checkIfFileExists("state");
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
