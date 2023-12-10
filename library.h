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

double getTotalValueOfAssets();

double getTotalBonus();

}

#endif //NF_TX_CORE_LIBRARY_H
