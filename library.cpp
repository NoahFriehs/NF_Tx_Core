#include "library.h"
#include "FileLog.h"
#include "DataHolder.h"
#include "TransactionParser.h"
#include "TransactionManager.h"
#include "TimeSpan.h"

#include <iostream>
#include <utility>
//#include <jni.h>





bool init() {

    if (DataHolder::GetInstance().isInitialized()) {
        FileLog::i("library", "Already initialized");
        return true;
    }

    FileLog::init("my_log.txt", true, 3);

    FileLog::i("library", "Initializing...");
    DataHolder::GetInstance().SetTransactionManager(new TransactionManager());
    if (DataHolder::GetInstance().checkSavedData()) {
        FileLog::i("library", "Saved data found, loading...");
        DataHolder::GetInstance().loadData();
        FileLog::i("library", "Done");
        return true;
    }
    FileLog::i("library", "No saved data found");
    return false;
}

bool initWithData(const std::vector<std::string> &data, uint mode) {

    FileLog::init("my_log.txt", true, 3);

    FileLog::i("library", "Initializing with data...");
    FileLog::i("library", "Data size: " + std::to_string(data.size()));

    // init DataHolder
    DataHolder &dataHolder = DataHolder::GetInstance();

    TimeSpan timeSpan;
    timeSpan.start();

    // parseFromCsv data
    TransactionParser parser(data);
    try {
        parser.parseFromCsv(static_cast<Mode>(mode));
    } catch (std::exception &e) {
        FileLog::e("library", e.what());
        return false;
    }

    long double time = timeSpan.end();
    FileLog::i("library", "Parsing took " + std::to_string(time) + " milliseconds");

    auto *transactionManager = new TransactionManager();

    transactionManager->setTransactions(parser.getTransactions(), static_cast<Mode>(mode));

    timeSpan.start();

    transactionManager->processTransactions();

    time = timeSpan.end();
    FileLog::i("library", "Processing took " + std::to_string(time) + " milliseconds");

    dataHolder.SetTransactionManager(transactionManager);

    // return true if successful
    return dataHolder.isInitialized();
}

std::vector<std::string> getCurrencies() {
    return DataHolder::GetInstance().GetTransactionManager()->getCurrencies();
}

void setPrice(std::vector<double> prices) {
    FileLog::i("library", "Setting prices and calculating wallet balances...");
    DataHolder::GetInstance().GetTransactionManager()->setPrices(std::move(prices));
    DataHolder::GetInstance().GetTransactionManager()->calculateWalletBalances();
    FileLog::i("library", "Done");
}


double getTotalMoneySpent() {
    return DataHolder::GetInstance().GetTransactionManager()->getTotalMoneySpent();
}

double getTotalValueOfAssets() {
    return DataHolder::GetInstance().GetTransactionManager()->getTotalValueOfAssets();
}

double getTotalBonus() {
    return DataHolder::GetInstance().GetTransactionManager()->getTotalBonus();
}

double getValueOfAssets(int walletId) {
    return DataHolder::GetInstance().GetTransactionManager()->getValueOfAssets(walletId);
}

double getBonus(int walletId) {
    return DataHolder::GetInstance().GetTransactionManager()->getTotalBonus(walletId);
}

double getMoneySpent(int walletId) {
    return DataHolder::GetInstance().GetTransactionManager()->getMoneySpent(walletId);
}



std::vector<std::string> getWalletsAsStrings() {
    std::map<std::string, Wallet> wallets = DataHolder::GetInstance().GetTransactionManager()->getWallets();
    std::vector<std::string> vec;
    for (auto &wallet: wallets) {
        vec.emplace_back(wallet.second.getWalletData()->serializeToXml());
    }
    return vec;
}

std::vector<std::string> getTransactionsAsStrings() {
    std::vector<BaseTransaction> transactions = DataHolder::GetInstance().GetTransactionManager()->getTransactions();
    std::vector<std::string> vec;
    for (auto &tx: transactions) {
        vec.emplace_back(tx.getTransactionData().serializeToXml());
    }
    return vec;
}

std::vector<std::string> getCardWalletsAsStrings() {
    std::map<std::string, Wallet> wallets = DataHolder::GetInstance().GetTransactionManager()->getCardWallets();
    std::vector<std::string> vec;
    for (auto &wallet: wallets) {
        vec.emplace_back(wallet.second.getWalletData()->serializeToXml());
    }
    return vec;
}

std::vector<std::string> getCardTransactionsAsStrings() {
    std::vector<BaseTransaction> transactions = DataHolder::GetInstance().GetTransactionManager()->getCardTransactions();
    std::vector<std::string> vec;
    for (auto &tx: transactions) {
        vec.emplace_back(tx.getTransactionData().serializeToXml());
    }
    return vec;
}

double getTotalMoneySpentCard() {
    return DataHolder::GetInstance().GetTransactionManager()->getTotalMoneySpentCard();
}

double getTotalValueOfAssetsCard() {
    return DataHolder::GetInstance().GetTransactionManager()->getTotalValueOfAssetsCard();
}

double getTotalBonusCard() {
    return DataHolder::GetInstance().GetTransactionManager()->getTotalBonusCard();
}

std::string getWalletAsString(int walletId) {
    auto wallet = DataHolder::GetInstance().GetTransactionManager()->getWallet(walletId);
    if (wallet == nullptr) return "";
    return wallet->getWalletData()->serializeToXml();
}

void save() {
    DataHolder::GetInstance().saveData();
}

void loadData() {
    DataHolder::GetInstance().loadData();
}

void calculateBalances() {
    DataHolder::GetInstance().GetTransactionManager()->calculateWalletBalances();
}






/**
extern "C"
JNIEXPORT jboolean JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_init(JNIEnv *env, jobject thiz) {
    return init();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_initWithData(JNIEnv *env, jobject thiz,
                                                                    jobjectArray data,
                                                                    jint dataSize, jint mode) {
    std::vector<std::string> vec;
    jsize len = env->GetArrayLength(data);
    for (int i = 0; i < len; i++) {
        auto string = (jstring) env->GetObjectArrayElement(data, i);
        const char *rawString = env->GetStringUTFChars(string, nullptr);
        vec.emplace_back(rawString);
        env->ReleaseStringUTFChars(string, rawString);
    }
    return initWithData(vec, mode);
}
extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getCurrencies(JNIEnv *env, jobject thiz) {
    std::vector<std::string> currencies = getCurrencies();
    jobjectArray ret = env->NewObjectArray(currencies.size(), env->FindClass("java/lang/String"),
                                           nullptr);
    for (int i = 0; i < currencies.size(); i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(currencies[i].c_str()));
    }
    return ret;
}
extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_setPrice(JNIEnv *env, jobject thiz,
                                                                jobjectArray prices) {
    std::vector<double> vec;
    jsize len = env->GetArrayLength(prices);
    for (int i = 0; i < len; i++) {
        auto price = env->GetObjectArrayElement(prices, i);
        if (price != nullptr) {
            jclass clazz = env->GetObjectClass(price);
            jmethodID methodID = env->GetMethodID(clazz, "doubleValue", "()D");
            double value = env->CallDoubleMethod(price, methodID);
            vec.emplace_back(value);
        }
    }
    setPrice(vec);
}


extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getTotalMoneySpent(JNIEnv *env,
    jobject thiz) {
    return getTotalMoneySpent();
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getValueOfAssets(JNIEnv *env, jobject thiz) {
    return getTotalValueOfAssets();
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getTotalBonus(JNIEnv *env, jobject thiz) {
    return getTotalBonus();
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_00024Companion_getValueOfAssets(JNIEnv *env,
    jobject thiz, jint wallet_id) {
    return getValueOfAssets(wallet_id);
}
extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getTransactionsAsString(JNIEnv *env,
    jobject thiz) {
    std::vector<std::string> vec = getTransactionsAsStrings();
    jobjectArray ret = env->NewObjectArray(vec.size(), env->FindClass("java/lang/String"), nullptr);
    for (int i = 0; i < vec.size(); i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(vec[i].c_str()));
    }
    return ret;
}
extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getWalletsAsString(JNIEnv *env,
    jobject thiz) {
    std::vector<std::string> vec = getWalletsAsStrings();
    jobjectArray ret = env->NewObjectArray(vec.size(), env->FindClass("java/lang/String"), nullptr);
    for (int i = 0; i < vec.size(); i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(vec[i].c_str()));
    }
    return ret;
}


extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getValueOfAssetsByWID(JNIEnv *env,
    jobject thiz, jint wallet_id) {
    return getValueOfAssets(wallet_id);
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getTotalBonusByWID(JNIEnv *env, jobject thiz,
    jint wallet_id) {
    return getBonus(wallet_id);
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_Core_CoreService_getMoneySpentByWID(JNIEnv *env, jobject thiz,
    jint wallet_id) {
    return getMoneySpent(wallet_id);
}
 **/