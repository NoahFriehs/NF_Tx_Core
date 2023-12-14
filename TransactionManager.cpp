//
// Created by nfriehs on 11/7/23.
//

#include <stdexcept>
#include "TransactionManager.h"
#include "FileLog.h"

TransactionManager::TransactionManager() = default;

TransactionManager::TransactionManager(std::vector<BaseTransaction> &transactions) {
    std::lock_guard<std::mutex> lock(mutex, std::adopt_lock);
    if (transactions.empty()) throw std::invalid_argument("Transactions is empty");

    this->transactions = transactions;
}

TransactionManager::~TransactionManager() = default;


void TransactionManager::processTransactions() {
    std::lock_guard<std::mutex> lock(mutex, std::adopt_lock);
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
    if (!transactions.empty()) for (auto &item: transactions) {
        hasTxData = true;
        if (std::find(currencies.begin(), currencies.end(), item.getCurrencyType()) ==
            currencies.end())
            currencies.push_back(item.getCurrencyType());
    }
    if (!cardTransactions.empty()) for (auto &item: cardTransactions) {
        hasCardTxData = true;
        if (std::find(cardTxTypes.begin(), cardTxTypes.end(), item.getTransactionTypeString()) ==
            cardTxTypes.end())
            cardTxTypes.push_back(item.getTransactionTypeString());
    }
}


bool TransactionManager::isReady() {
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
            cardWallets["EUR -> EUR"].addTransaction(tx);
            continue;
        }
        auto *wallet = getNonStrictWallet(tt);
        wallet->addTransaction(tx);
    }
}

void TransactionManager::addCDCTransactionsToWallets() {
    for (auto &tx: transactions) {
        FileLog::v("TransactionManager", "Adding transaction to wallet: " + tx.getCurrencyType());
        // add transaction to wallet
        auto *wallet = &wallets[tx.getCurrencyType()];
        tx.setWalletId(wallet->getWalletId());
        tx.setFromWalletId(wallet->getWalletId());

        switch (tx.getTransactionType()) {
            case dust_conversion_credited:
            case crypto_purchase:
                wallet->addTransaction(tx);
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
                wallet->addTransaction(tx);
                break;
            case viban_purchase:
                vibianPurchase(tx);
                break;
            case crypto_withdrawal:
                wallet->addTransaction(tx);
                outWallets[tx.getCurrencyType()].withdraw(tx);
                break;
            case crypto_deposit:    //TODO: check if this is correct with the new data, we have no Tx for this until now
                wallet->addTransaction(tx);
                outWallets[tx.getCurrencyType()].withdraw(tx);
                break;
            case crypto_viban_exchange:
                wallet->withdraw(tx);
                wallets["EUR"].addTransaction(tx);
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
                break;
        }

    }
}

void TransactionManager::vibianPurchase(BaseTransaction &tx) {
    auto *toWallet = &wallets[tx.getToCurrencyType()];
    auto *wallet = &wallets[tx.getCurrencyType()];

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

    walletBalanceMap.clear();
    cardWalletBalanceMap.clear();
    walletsBalance.reset();
    cardWalletsBalance.reset();
    if (hasTxData) for (auto &wallet: wallets) {

        if (wallet.second.getCurrencyType() == "EUR") continue;

        auto *walletBalance = new WalletBalance();
        walletBalance->fillFromWallet(&wallet.second);
        if (walletBalance->nativeBalance == 0 && walletBalance->balance != 0) {
            walletBalance->nativeBalance =
                    assetValue.getPrice(walletBalance->currencyType) * walletBalance->balance;
            walletBalance->nativeBonusBalance =
                    assetValue.getPrice(walletBalance->currencyType) * walletBalance->bonusBalance;
        }
        walletBalanceMap.insert(
                std::pair<std::string, WalletBalance>(wallet.first, *walletBalance));
    }
    walletsBalance.fillFromWalletBalanceMap(walletBalanceMap);

    if (hasCardTxData) for (auto [txType, wallet]: cardWallets) {
        auto *walletBalance = new WalletBalance();
        walletBalance->fillFromWallet(&wallet);
        if (walletBalance->nativeBalance == 0 && walletBalance->balance != 0) {
            walletBalance->nativeBalance =
                    assetValue.getPrice(walletBalance->currencyType) * walletBalance->balance;
            walletBalance->nativeBonusBalance =
                    assetValue.getPrice(walletBalance->currencyType) * walletBalance->bonusBalance;
        }
        cardWalletBalanceMap.insert(
                std::pair<std::string, WalletBalance>(txType, *walletBalance));
    }
    cardWalletsBalance.fillFromWalletBalanceMap(cardWalletBalanceMap);

}

std::vector<std::string> TransactionManager::getCurrencies() {
    return currencies;
}

void TransactionManager::setPrices(std::vector<double> prices) {
    assetValue.loadCacheWithData(currencies, prices);
}

std::vector<BaseTransaction> TransactionManager::getTransactions() {
    return transactions;
}

double TransactionManager::getTotalMoneySpent() {
    return walletsBalance.moneySpent;
}

double TransactionManager::getTotalValueOfAssets() {
    return walletsBalance.nativeBalance;
}

double TransactionManager::getTotalBonus() {
    return walletsBalance.nativeBonusBalance;
}

double TransactionManager::getValueOfAssets(int walletId) {
    for (const auto &item: walletBalanceMap){
        if (item.second.walletId == walletId)
            return item.second.nativeBalance;
    }
    for (const auto &item: cardWalletBalanceMap){
        if (item.second.walletId == walletId)
            return item.second.nativeBalance;
    }
    FileLog::w("TransactionsManager", "No wallet found for id: " + std::to_string(walletId));
    return 0.0;
}

std::map<std::string, Wallet> TransactionManager::getWallets() {
    return wallets;
}

double TransactionManager::getTotalBonus(int walletId) {
    for (const auto &item: walletBalanceMap){
        if (item.second.walletId == walletId)
            return item.second.nativeBonusBalance;
    }
    for (const auto &item: cardWalletBalanceMap){
        if (item.second.walletId == walletId)
            return item.second.nativeBonusBalance;
    }
    FileLog::w("TransactionsManager", "No wallet found for id: " + std::to_string(walletId));
    return 0.0;
}

double TransactionManager::getMoneySpent(int walletId) {
    for (const auto &item: walletBalanceMap){
        if (item.second.walletId == walletId)
            return item.second.moneySpent;
    }
    for (const auto &item: cardWalletBalanceMap){
        if (item.second.walletId == walletId)
            return item.second.moneySpent;
    }
    FileLog::w("TransactionsManager", "No wallet found for id: " + std::to_string(walletId));
    return 0.0;
}

void TransactionManager::setTransactions(std::vector<BaseTransaction> &transactions_, Mode mode) {
    std::lock_guard<std::mutex> lock(mutex, std::adopt_lock);
    if (transactions_.empty()) throw std::invalid_argument("Transactions is empty");

    switch (mode) {
        case CDC:
            this->transactions = transactions_;
            hasTxData = true;
            break;
        case Card:
            this->cardTransactions = transactions_;
            hasCardTxData = true;
            break;
        case Custom:
            throw std::invalid_argument("Custom mode not implemented");
            break;
        case Default:
            this->transactions = transactions_;
            hasTxData = true;
            break;
    }
}

std::string TransactionManager::checkCardTxTypes(const std::string& tt, const std::string& txType) {
    auto it = std::find(cardTxTypes.begin(), cardTxTypes.end(), tt);
    if (it != cardTxTypes.end()) {
        cardTxTypes.erase(it);
    }
    cardTxTypes.push_back(txType);
    return txType;
}

std::string TransactionManager::checkForRefund(std::string& tt) {
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

    for (auto& [name, w] : cardWallets) {
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
    return &cardWallets[modifiedTT];
}

std::map<std::string, Wallet> TransactionManager::getCardWallets() {
    return cardWallets;
}

std::vector<BaseTransaction> TransactionManager::getCardTransactions() {
    return cardTransactions;
}

double TransactionManager::getTotalValueOfAssetsCard() {
    return cardWalletsBalance.nativeBalance;
}

double TransactionManager::getTotalBonusCard() {
    return cardWalletsBalance.nativeBonusBalance;
}

double TransactionManager::getTotalMoneySpentCard() {
    return cardWalletsBalance.moneySpent;
}

std::unique_ptr<Wallet> TransactionManager::getWallet(int walletId) {
    if (hasTxData) for (auto [txType, wallet]: wallets){
        if (wallet.getWalletId() == walletId)
            return std::make_unique<Wallet>(wallet);
    }
    if (hasCardTxData) for (auto [txType, wallet]: cardWallets){
        if (wallet.getWalletId() == walletId)
            return std::make_unique<Wallet>(wallet);
    }
    FileLog::e("TransactionsManager", "No wallet found for id: " + std::to_string(walletId));
    return nullptr;
}
