#ifndef NF_TX_CORE_LIBRARY_H
#define NF_TX_CORE_LIBRARY_H

#include <vector>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

void hello();

bool init();

bool initWithData(const std::vector<std::string>& data, uint mode);

#ifdef __cplusplus
}
#endif

#endif //NF_TX_CORE_LIBRARY_H
