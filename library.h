#ifndef NF_TX_CORE_LIBRARY_H
#define NF_TX_CORE_LIBRARY_H

#include <vector>
#include <string>

extern "C" {


bool init();

bool initWithData(const std::vector<std::string> &data, uint mode);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"  // Ignore warning about C++ linkage
std::vector<std::string> getCurrencies();
#pragma clang diagnostic pop

void setPrice(std::vector<double> prices);

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
