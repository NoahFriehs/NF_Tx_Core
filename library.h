#ifndef NF_TX_CORE_LIBRARY_H
#define NF_TX_CORE_LIBRARY_H

#include <vector>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

void hello();

bool init();

bool initWithData(const std::vector<std::string> &data, uint mode);

std::vector<std::string> getCurrencies();   //Kotlin: List<String> getCurrencies();

void setPrice(std::vector<double> prices);  //Kotlin: void setPrice(List<Double> prices);

#ifdef __cplusplus
}
#endif

#endif //NF_TX_CORE_LIBRARY_H
