#include "library.h"
#include "FileLog.h"
#include "DataHolder.h"
#include "TransactionParser.h"
#include "TransactionManager.h"
#include "TimeSpan.h"
#include <memory>

#ifdef __ANDROID__
#include <jni.h>
#endif







bool init(const std::string &logFilePath, const std::string &loadDirPath) {

    FileLog::init(logFilePath, true, 3);

    FileLog::i("library", "Initializing...");

    if (DataHolder::GetInstance().isInitialized()) {
        FileLog::i("library", "Already initialized");
        return true;
    }

    auto transactionManager = std::make_unique<TransactionManager>();

    DataHolder::GetInstance().SetTransactionManager(std::move(transactionManager));
    if (DataHolder::GetInstance().checkSavedData() && !loadDirPath.empty()) {
        FileLog::i("library", "Saved data found, loading...");
        DataHolder::GetInstance().loadData(loadDirPath);
        FileLog::i("library", "Done");
        return true;
    }
    FileLog::i("library", "No saved data found");
    return false;
}

bool initWithData(const std::vector<std::string> &data, int mode, const std::string &logFilePath) {

    FileLog::init(logFilePath, true, 3);

    FileLog::i("library", "Initializing with data, with mode " + std::to_string(mode) + "...");

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

    auto transactionManager = std::make_unique<TransactionManager>();

    transactionManager->setTransactions(parser.getTransactions(), static_cast<Mode>(mode));

    timeSpan.start();

    transactionManager->processTransactions();

    end = timeSpan.end();
    FileLog::i("library", "Processing took " + std::to_string(end) + " milliseconds");

    dataHolder.SetTransactionManager(std::move(transactionManager));

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
    const auto &wallets = DataHolder::GetInstance().GetTransactionManager()->getWallets();
    std::vector<std::string> vec;
    vec.reserve(wallets.size());
    for (const auto &wallet: wallets) {
        vec.emplace_back(wallet.second.getWalletData()->serializeToXml());
    }
    FileLog::i("library", "Returning " + std::to_string(vec.size()) + " wallets");
    return vec;
}

std::vector<std::string> getTransactionsAsStrings() {
    const auto &transactions = DataHolder::GetInstance().GetTransactionManager()->getTransactions();
    std::vector<std::string> vec;
    vec.reserve(transactions.size());
    for (const auto &tx: transactions) {
        vec.emplace_back(tx.getTransactionData().serializeToXml());
    }
    FileLog::i("library", "Returning " + std::to_string(vec.size()) + " transactions");
    return vec;
}

std::vector<std::string> getCardWalletsAsStrings() {
    const auto &wallets = DataHolder::GetInstance().GetTransactionManager()->getCardWallets();
    std::vector<std::string> vec;
    vec.reserve(wallets.size());
    for (const auto &wallet: wallets) {
        vec.emplace_back(wallet.second.getWalletData()->serializeToXml());
    }
    FileLog::i("library", "Returning " + std::to_string(vec.size()) + " card wallets");
    return vec;
}

std::vector<std::string> getCardTransactionsAsStrings() {
    const auto &transactions = DataHolder::GetInstance().GetTransactionManager()->getCardTransactions();
    std::vector<std::string> vec;
    vec.reserve(transactions.size());
    for (const auto &tx: transactions) {
        vec.emplace_back(tx.getTransactionData().serializeToXml());
    }
    FileLog::i("library", "Returning " + std::to_string(vec.size()) + " card transactions");
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

#ifdef __ANDROID__

namespace {

// RAII wrapper keeping GetStringUTFChars/ReleaseStringUTFChars in lockstep.
struct ScopedUtfChars {
    JNIEnv *env;
    jstring string;
    const char *chars;

    ScopedUtfChars(JNIEnv *env_, jstring string_) : env(env_), string(string_), chars(nullptr) {
        if (env != nullptr && string != nullptr) {
            chars = env->GetStringUTFChars(string, nullptr);
        }
    }

    ~ScopedUtfChars() {
        if (env != nullptr && string != nullptr && chars != nullptr) {
            env->ReleaseStringUTFChars(string, chars);
        }
    }

    explicit operator bool() const { return chars != nullptr; }

    std::string value() const { return chars == nullptr ? std::string() : std::string(chars); }
};

// Build a java.lang.String[] from std::string values, releasing every
// temporary local reference (including the per-element jstrings) as we go.
jobjectArray stringsToJArray(JNIEnv *env, const std::vector<std::string> &values) {
    jclass stringClass = env->FindClass("java/lang/String");
    if (stringClass == nullptr) return nullptr;
    auto result = env->NewObjectArray(static_cast<jsize>(values.size()), stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    if (result == nullptr) return nullptr;
    for (size_t i = 0; i < values.size(); i++) {
        jstring value = env->NewStringUTF(values[i].c_str());
        if (value == nullptr) break;
        env->SetObjectArrayElement(result, static_cast<jsize>(i), value);
        env->DeleteLocalRef(value);
    }
    return result;
}

// Read a java.lang.String[] into std::vector<std::string>.
bool jArrayToStrings(JNIEnv *env, jobjectArray array, std::vector<std::string> &out) {
    if (array == nullptr) return false;
    jsize len = env->GetArrayLength(array);
    out.reserve(static_cast<size_t>(len));
    for (jsize i = 0; i < len; i++) {
        auto element = env->GetObjectArrayElement(array, i);
        if (element == nullptr) continue;
        ScopedUtfChars utf(env, (jstring) element);
        env->DeleteLocalRef(element);
        if (utf) out.emplace_back(utf.value());
    }
    return true;
}

// Read a boxed Double[] (java.lang.Double elements) into std::vector<double>.
bool jArrayToDoubles(JNIEnv *env, jobjectArray array, std::vector<double> &out) {
    if (array == nullptr) return false;
    jsize len = env->GetArrayLength(array);
    out.reserve(static_cast<size_t>(len));
    for (jsize i = 0; i < len; i++) {
        auto element = env->GetObjectArrayElement(array, i);
        if (element == nullptr) continue;
        jclass clazz = env->GetObjectClass(element);
        jmethodID doubleValue =
                clazz != nullptr ? env->GetMethodID(clazz, "doubleValue", "()D") : nullptr;
        if (doubleValue != nullptr) {
            out.push_back(env->CallDoubleMethod(element, doubleValue));
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        if (clazz != nullptr) env->DeleteLocalRef(clazz);
        env->DeleteLocalRef(element);
    }
    return true;
}

} // namespace


extern "C"
JNIEXPORT jboolean JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_init(JNIEnv *env, jobject,
                                                            jstring path,
                                                            jstring loadDirPath) {
    try {
        ScopedUtfChars pathChars(env, path);
        ScopedUtfChars loadDirChars(env, loadDirPath);
        if (!pathChars || !loadDirChars) return JNI_FALSE;
        return init(pathChars.value(), loadDirChars.value()) ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI init failed: " + std::string(e.what()));
        return JNI_FALSE;
    } catch (...) {
        FileLog::e("library", "JNI init failed: unknown error");
        return JNI_FALSE;
    }
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_initWithData(JNIEnv *env, jobject,
                                                                    jobjectArray data,
                                                                    jint, jint mode, jstring path) {
    try {
        std::vector<std::string> lines;
        if (!jArrayToStrings(env, data, lines)) return JNI_FALSE;
        ScopedUtfChars pathChars(env, path);
        if (!pathChars) return JNI_FALSE;
        return initWithData(lines, mode, pathChars.value()) ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI initWithData failed: " + std::string(e.what()));
        return JNI_FALSE;
    } catch (...) {
        FileLog::e("library", "JNI initWithData failed: unknown error");
        return JNI_FALSE;
    }
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getCurrencies(JNIEnv *env, jobject) {
    try {
        return stringsToJArray(env, getCurrencies());
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getCurrencies failed: " + std::string(e.what()));
        return nullptr;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_setPrice(JNIEnv *env, jobject,
                                                                jobjectArray prices) {
    try {
        std::vector<double> values;
        if (!jArrayToDoubles(env, prices, values)) return;
        setPrice(values);
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI setPrice failed: " + std::string(e.what()));
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTotalMoneySpent(JNIEnv *, jobject) {
    try {
        return getTotalMoneySpent();
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getTotalMoneySpent failed: " + std::string(e.what()));
        return 0.0;
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTotalMoneySpentCard(JNIEnv *,
                                                                              jobject) {
    try {
        return getTotalMoneySpentCard();
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getTotalMoneySpentCard failed: " + std::string(e.what()));
        return 0.0;
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getValueOfAssets(JNIEnv *, jobject) {
    try {
        return getTotalValueOfAssets();
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getValueOfAssets failed: " + std::string(e.what()));
        return 0.0;
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTotalBonus(JNIEnv *, jobject) {
    try {
        return getTotalBonus();
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getTotalBonus failed: " + std::string(e.what()));
        return 0.0;
    }
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTransactionsAsString(JNIEnv *env,
                                                                               jobject) {
    try {
        return stringsToJArray(env, getTransactionsAsStrings());
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getTransactionsAsString failed: " + std::string(e.what()));
        return nullptr;
    }
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getCardTransactionsAsStrings(JNIEnv *env,
                                                                                    jobject) {
    try {
        return stringsToJArray(env, getCardTransactionsAsStrings());
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getCardTransactionsAsStrings failed: " +
                               std::string(e.what()));
        return nullptr;
    }
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getWalletsAsString(JNIEnv *env,
                                                                          jobject) {
    try {
        return stringsToJArray(env, getWalletsAsStrings());
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getWalletsAsString failed: " + std::string(e.what()));
        return nullptr;
    }
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getCardWalletsAsStrings(JNIEnv *env,
                                                                               jobject) {
    try {
        return stringsToJArray(env, getCardWalletsAsStrings());
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getCardWalletsAsStrings failed: " +
                               std::string(e.what()));
        return nullptr;
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getValueOfAssetsByWID(JNIEnv *, jobject,
                                                                             jint wallet_id) {
    try {
        return getValueOfAssets(wallet_id);
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getValueOfAssetsByWID failed: " + std::string(e.what()));
        return 0.0;
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getTotalBonusByWID(JNIEnv *, jobject,
                                                                          jint wallet_id) {
    try {
        return getBonus(wallet_id);
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getTotalBonusByWID failed: " + std::string(e.what()));
        return 0.0;
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getMoneySpentByWID(JNIEnv *, jobject,
                                                                          jint wallet_id) {
    try {
        return getMoneySpent(wallet_id);
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getMoneySpentByWID failed: " + std::string(e.what()));
        return 0.0;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_save(JNIEnv *env, jobject, jstring path) {
    try {
        ScopedUtfChars pathChars(env, path);
        if (!pathChars) return;
        save(pathChars.value());
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI save failed: " + std::string(e.what()));
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_load(JNIEnv *env, jobject, jstring path) {
    try {
        ScopedUtfChars pathChars(env, path);
        if (!pathChars) return;
        loadData(pathChars.value());
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI load failed: " + std::string(e.what()));
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_getModes(JNIEnv *, jobject) {
    try {
        return getActiveModes();
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI getModes failed: " + std::string(e.what()));
        return 0;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_setTransactionData(JNIEnv *env, jobject,
                                                                          jobjectArray data) {
    try {
        std::vector<std::string> lines;
        if (!jArrayToStrings(env, data, lines)) return;
        setTransactionData(lines);
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI setTransactionData failed: " + std::string(e.what()));
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_setWalletData(JNIEnv *env, jobject,
                                                                     jobjectArray data) {
    try {
        std::vector<std::string> lines;
        if (!jArrayToStrings(env, data, lines)) return;
        setWalletData(lines);
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI setWalletData failed: " + std::string(e.what()));
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_setCardTransactionData(JNIEnv *env,
                                                                              jobject,
                                                                              jobjectArray data) {
    try {
        std::vector<std::string> lines;
        if (!jArrayToStrings(env, data, lines)) return;
        setCardTransactionData(lines);
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI setCardTransactionData failed: " + std::string(e.what()));
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_setCardWalletData(JNIEnv *env,
                                                                         jobject,
                                                                         jobjectArray data) {
    try {
        std::vector<std::string> lines;
        if (!jArrayToStrings(env, data, lines)) return;
        setCardWalletData(lines);
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI setCardWalletData failed: " + std::string(e.what()));
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_at_msd_friehs_1bicha_cdcsvparser_core_CoreService_calculateWalletBalances(JNIEnv *,
                                                                               jobject) {
    try {
        calculateBalances();
    } catch (const std::exception &e) {
        FileLog::e("library", "JNI calculateWalletBalances failed: " + std::string(e.what()));
    }
}

#endif // __ANDROID__
