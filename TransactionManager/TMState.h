//
// Created by nfriehs on 12/14/23.
//

#ifndef NF_TX_CORE_TMSTATE_H
#define NF_TX_CORE_TMSTATE_H


#include <cstring>
#include "../Structs.h"
#include "../Transaction/BaseTransaction.h"

class TMState {
public:
    bool isBig = false;
    int txIdCounter = 0;
    bool hasTxData = false;
    bool hasCardTxData = false;
    bool isReadyFlag = false;
    char currencies[MAX_WALLETS][MAX_STRING_LENGTH] = {};
    char cardTxTypes[MAX_WALLETS][MAX_STRING_LENGTH] = {};

    TransactionManagerState getTransactionManagerState() {
        TransactionManagerState state;
        state.isBig = isBig;
        state.txIdCounter = BaseTransaction::getTxIdCounter();
        state.hasTxData = hasTxData;
        state.hasCardTxData = hasCardTxData;
        state.isReadyFlag = isReadyFlag;
        for (int i = 0; i < MAX_WALLETS; i++) {
            strcpy(state.currencies[i], currencies[i]);
            strcpy(state.cardTxTypes[i], cardTxTypes[i]);
        }
        return state;
    }

};

class BigTMState : public TMState {
public:
    char bigCurrencies[BIG_MAX_WALLETS][MAX_STRING_LENGTH] = {};
    char bigCardTxTypes[BIG_MAX_WALLETS][MAX_STRING_LENGTH] = {};

    BigTMState() {
        isBig = true;
    }

    BigTransactionMangerState getBigTransactionManagerState() {
        BigTransactionMangerState state;
        state.isBig = isBig;
        state.hasTxData = hasTxData;
        state.hasCardTxData = hasCardTxData;
        state.isReadyFlag = isReadyFlag;
        for (int i = 0; i < MAX_WALLETS; i++) {
            strcpy(state.currencies[i], currencies[i]);
            strcpy(state.cardTxTypes[i], cardTxTypes[i]);
        }
        for (int i = 0; i < BIG_MAX_WALLETS; i++) {
            strcpy(state.bigCurrencies[i], bigCurrencies[i]);
            strcpy(state.bigCardTxTypes[i], bigCardTxTypes[i]);
        }
        return state;
    }
};


#endif //NF_TX_CORE_TMSTATE_H
