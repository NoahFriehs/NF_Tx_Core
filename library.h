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


bool init(const std::string &logFilePath, const std::string &loadDirPath);

bool initWithData(const std::vector<std::string> &data, uint mode, const std::string &logFilePath);

void save(const std::string &filePath);

void loadData(const std::string &filePath);

void calculate();

void clearAll();

void setWalletData(const std::vector<std::string> &data);

void setCardWalletData(const std::vector<std::string> &data);

void setTransactionData(const std::vector<std::string> &data);

void setCardTransactionData(const std::vector<std::string> &data);

void calculateBalances();

std::vector<std::string> getCurrencies();

void setPrice(const std::vector<double> &prices);

double getTotalMoneySpent();

double getTotalMoneySpentCard();

double getTotalValueOfAssets();

double getTotalValueOfAssetsCard();

double getTotalBonus();

double getTotalBonusCard();

double getValueOfAssets(int walletId);

double getBonus(int walletId);

double getMoneySpent(int walletId);

std::vector<std::string> getWalletsAsStrings();

std::vector<std::string> getCardWalletsAsStrings();

std::string getWalletAsString(int walletId);

std::vector<std::string> getTransactionsAsStrings();

std::vector<std::string> getCardTransactionsAsStrings();

}

#endif //NF_TX_CORE_LIBRARY_H

#pragma clang diagnostic pop