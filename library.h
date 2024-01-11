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
bool initWithData(const std::vector<std::string> &data, int mode, const std::string &logFilePath);

//! \brief Initializes the library with the given data, mode, log file path and load directory path.
void save(const std::string &filePath);

//! \brief Loads the saved data from the given dir path.
void loadData(const std::string &dirPath);

//! \brief Returns the active modes. (1 = Crypto, 2 = Card, 3 = Crypto + Card)
int getActiveModes();

void calculate();

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

//! \brief Returns the total money spent.
double getTotalMoneySpent();

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

}

#endif //NF_TX_CORE_LIBRARY_H

#pragma clang diagnostic pop