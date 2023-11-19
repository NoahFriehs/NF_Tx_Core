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
    for (auto &item: transactions) {
        if (std::find(currencies.begin(), currencies.end(), item.getCurrencyType()) == currencies.end())
            currencies.push_back(item.getCurrencyType());
    }
}


bool TransactionManager::isReady() {
    return isReadyFlag;
}

void TransactionManager::createWallets() {
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

void TransactionManager::addTransactionsToWallets() {
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
                FileLog::w("TransactionManager", "Unknown transaction type: " + tx.getTransactionTypeString());
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
            FileLog::w("TransactionManager", "Unused transaction: " + tx.getTransactionTypeString());
        }
    }

}

void TransactionManager::calculateWalletBalances() {

    walletBalanceMap.clear();
    for (auto &wallet: wallets) {

        if (wallet.second.getCurrencyType() == "EUR") continue;

        auto *walletBalance = new WalletBalance();
        walletBalance->fillFromWallet(&wallet.second);
        if (walletBalance->nativeBalance == 0 && walletBalance->balance != 0)
        {
            walletBalance->nativeBalance = assetValue.getPrice(walletBalance->currencyType) * walletBalance->balance;
            walletBalance->nativeBonusBalance = assetValue.getPrice(walletBalance->currencyType) * walletBalance->bonusBalance;
        }
        walletBalanceMap.insert(std::pair<std::string, WalletBalance>(wallet.first, *walletBalance));
    }
    walletsBalance.fillFromWalletBalanceMap(walletBalanceMap);

}

std::vector<std::string> TransactionManager::getCurrencies() {
    return currencies;
}

void TransactionManager::setPrices(std::vector<double> prices) {
    assetValue.loadCacheWithData(currencies, prices);
}
