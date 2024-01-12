#include "library.h"
#include "FileLog.h"
#include "DataHolder.h"
#include "TransactionParser.h"
#include "TransactionManager.h"
#include "TimeSpan.h"

#ifdef ANDROID
#include <jni.h>
#endif





bool init(const std::string &logFilePath, const std::string &loadDirPath) {

    FileLog::i("library", "Initializing...");

    if (DataHolder::GetInstance().isInitialized()) {
        FileLog::i("library", "Already initialized");
        return true;
    }

    FileLog::init(logFilePath, true, 3);

    DataHolder::GetInstance().SetTransactionManager(new TransactionManager());
    if (DataHolder::GetInstance().checkSavedData()) {
        FileLog::i("library", "Saved data found, loading...");
        DataHolder::GetInstance().loadData(loadDirPath);
        FileLog::i("library", "Done");
        return true;
    }
    FileLog::i("library", "No saved data found");
    return false;
}

bool initWithData(const std::vector<std::string> &data, int mode, const std::string &logFilePath) {

    FileLog::i("library", "Initializing with data...");

    FileLog::init(logFilePath, true, 3);

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

    long double end = timeSpan.end();
    FileLog::i("library", "Parsing took " + std::to_string(end) + " milliseconds");

    auto *transactionManager = new TransactionManager();

    transactionManager->setTransactions(parser.getTransactions(), static_cast<Mode>(mode));

    timeSpan.start();

    transactionManager->processTransactions();

    end = timeSpan.end();
    FileLog::i("library", "Processing took " + std::to_string(end) + " milliseconds");

    dataHolder.SetTransactionManager(transactionManager);

    // return true if successful
    return dataHolder.isInitialized();
}

std::vector<std::string> getCurrencies() {
    return DataHolder::GetInstance().GetTransactionManager()->getCurrencies();
}

void setPrice(const std::vector<double> &prices) {
    FileLog::i("library", "Setting prices and calculating wallet balances...");
    DataHolder::GetInstance().GetTransactionManager()->setPrices(prices);
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
    if (wallet == nullptr) {
        wallet = DataHolder::GetInstance().GetTransactionManager()->getCardWallet(walletId);
    }
    if (wallet == nullptr) return "";
    return wallet->getWalletData()->serializeToXml();
}

void save(const std::string &filePath) {
    DataHolder::GetInstance().saveData(filePath);
}

void loadData(const std::string &dirPath) {
    DataHolder::GetInstance().loadData(dirPath);
}

void calculateBalances() {
    DataHolder::GetInstance().GetTransactionManager()->calculateWalletBalances();
}

void setWalletData(const std::vector<std::string> &data) {
    std::vector<WalletData> vec;
    for (auto &wallet: data) {
        WalletData walletData;
        WalletData::deserializeFromXml(wallet, walletData);
        vec.emplace_back(walletData);
    }
    DataHolder::GetInstance().GetTransactionManager()->setWalletData(vec);
}

void setCardWalletData(const std::vector<std::string> &data) {
    std::vector<WalletData> vec;
    for (auto &wallet: data) {
        WalletData walletData;
        WalletData::deserializeFromXml(wallet, walletData);
        vec.emplace_back(walletData);
    }
    DataHolder::GetInstance().GetTransactionManager()->setCardWalletData(vec);
}

void setTransactionData(const std::vector<std::string> &data) {
    std::vector<TransactionData> vec;
    for (auto &tx: data) {
        TransactionData txData;
        txData.deserializeFromXml(tx);
        vec.emplace_back(txData);
    }
    DataHolder::GetInstance().GetTransactionManager()->setTransactionData(vec);
}

void setCardTransactionData(const std::vector<std::string> &data) {
    std::vector<TransactionData> vec;
    for (auto &tx: data) {
        TransactionData txData;
        txData.deserializeFromXml(tx);
        vec.emplace_back(txData);
    }
    DataHolder::GetInstance().GetTransactionManager()->setCardTransactionData(vec);
}

void clearAll() {
    DataHolder::GetInstance().GetTransactionManager()->clearAll();
}

int getActiveModes() {
    return DataHolder::GetInstance().GetTransactionManager()->getActiveModes();
}


#ifdef ANDROID
extern "C"
JNIEXPORT jboolean JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_init(JNIEnv *env, jobject thiz, jstring path, jstring loadDirPath) {
    const char *rawString = env->GetStringUTFChars(path, nullptr);
    const char *rawString2 = env->GetStringUTFChars(loadDirPath, nullptr);
    std::string filePath = rawString;
    std::string loadDir = rawString2;
    env->ReleaseStringUTFChars(path, rawString);
    env->ReleaseStringUTFChars(loadDirPath, rawString2);
    return init(filePath, loadDir);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_initWithData(JNIEnv *env, jobject thiz,
                                                                    jobjectArray data,
                                                                    jint dataSize, jint mode, jstring path) {
    std::vector<std::string> vec;
    jsize len = env->GetArrayLength(data);
    for (int i = 0; i < len; i++) {
        auto string = (jstring) env->GetObjectArrayElement(data, i);
        const char *rawString = env->GetStringUTFChars(string, nullptr);
        vec.emplace_back(rawString);
        env->ReleaseStringUTFChars(string, rawString);
    }
    const char *rawString = env->GetStringUTFChars(path, nullptr);
    std::string filePath = rawString;
    env->ReleaseStringUTFChars(path, rawString);
    return initWithData(vec, mode, filePath);
}
extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getCurrencies(JNIEnv *env, jobject thiz) {
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
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_setPrice(JNIEnv *env, jobject thiz,
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
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTotalMoneySpent(JNIEnv *env,
                                                                          jobject thiz) {
    return getTotalMoneySpent();
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getValueOfAssets(JNIEnv *env, jobject thiz) {
    return getTotalValueOfAssets();
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTotalBonus(JNIEnv *env, jobject thiz) {
    return getTotalBonus();
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTransactionsAsString(JNIEnv *env,
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
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getWalletsAsString(JNIEnv *env,
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
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getValueOfAssetsByWID(JNIEnv *env,
                                                                             jobject thiz,
                                                                             jint wallet_id) {
    return getValueOfAssets(wallet_id);
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTotalBonusByWID(JNIEnv *env, jobject thiz,
                                                                          jint wallet_id) {
    return getBonus(wallet_id);
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getMoneySpentByWID(JNIEnv *env, jobject thiz,
                                                                          jint wallet_id) {
    return getMoneySpent(wallet_id);
}
extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_save(JNIEnv *env, jobject thiz, jstring path) {
    const char *rawString = env->GetStringUTFChars(path, nullptr);
    std::string filePath = rawString;
    save(filePath);
    env->ReleaseStringUTFChars(path, rawString);
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_load(JNIEnv *env, jobject thiz, jstring path) {
    std::string filePath = env->GetStringUTFChars(path, nullptr);
    loadData(filePath);
    env->ReleaseStringUTFChars(path, filePath.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_calculateWalletBalances(JNIEnv *env,
                                                                               jobject thiz) {
    calculateBalances();
}

#endif