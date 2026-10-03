// Native binary persistence (format v3).
//
// The v3 format is an explicit, portable byte layout:
//   * all integers   : fixed width, little-endian
//   * all money      : 8-byte IEEE-754 double (the app never produces
//                      sub-double precision, so no ABI-dependent
//                      long double is ever stored)
//   * all strings    : u32 byte length + UTF-8 bytes
//   * all lists      : u32 element count + elements
//   * all dates      : wall-clock fields (y/m/d/h/min/s), no time zone,
//                      exactly as displayed
//
// Every file starts with the 5-byte header "CWCP" + version(3). The
// header carries no ABI data, so the format is portable between
// architectures, compilers and endiannesses.
//
// Old v2 files (raw memory dumps of fixed-size C structs) can still be
// READ so user data survives the upgrade; the next save rewrites them
// in v3. Reading v2 requires the same long double ABI that wrote the
// file (checked via the v2 header) - a v2 file from another ABI is
// rejected, exactly like before.
//
// Writes are atomic: data goes to "<name>.tmp" and is renamed over the
// target, so a crash mid-save can never corrupt the previous file.

#ifndef NF_TX_CORE_BINARYUTIL_H
#define NF_TX_CORE_BINARYUTIL_H


#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <functional>
#include <filesystem>
#include "FileLog.h"
#include "Structs.h"
#include "Enums.h"
#include "Util/Util.h"   // TimestampConverter

namespace BinaryUtil {

inline constexpr char kMagic[4] = {'C', 'W', 'C', 'P'};
inline constexpr uint8_t kVersionV2 = 2;
inline constexpr uint8_t kVersionV3 = 3;

// ---------------------------------------------------------------------------
// Low-level little-endian writer
// ---------------------------------------------------------------------------
class Writer {
public:
    explicit Writer(std::ofstream &f) : f_(f) {}

    bool ok() const { return f_.good(); }

    void u8(uint8_t v) {
        char c = static_cast<char>(v);
        f_.write(&c, 1);
    }

    void u32(uint32_t v) {
        char b[4];
        for (int i = 0; i < 4; i++) b[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
        f_.write(b, 4);
    }

    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }

    void i64(int64_t v) {
        char b[8];
        for (int i = 0; i < 8; i++) b[i] = static_cast<char>((static_cast<uint64_t>(v) >> (8 * i)) & 0xFF);
        f_.write(b, 8);
    }

    void f64(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        i64(static_cast<int64_t>(bits));
    }

    void raw(const void *p, size_t n) { f_.write(static_cast<const char *>(p), n); }

    void string(const std::string &s) {
        u32(static_cast<uint32_t>(s.size()));
        if (!s.empty()) raw(s.data(), s.size());
    }

    void count(size_t n) { u32(static_cast<uint32_t>(n)); }

    std::ofstream &f_;
};

// ---------------------------------------------------------------------------
// Low-level little-endian reader. Bounds for size-prefixed reads come from
// the remaining file bytes, so a corrupt length can neither over-allocate
// nor stall.
// ---------------------------------------------------------------------------
class Reader {
public:
    bool open(const std::string &path) {
        f_.open(path, std::ios::binary);
        if (!f_.is_open()) {
            FileLog::e("BinaryUtil", "Cannot open " + path);
            return false;
        }
        f_.seekg(0, std::ios::end);
        size_ = static_cast<size_t>(f_.tellg());
        f_.seekg(0, std::ios::beg);
        return true;
    }

    bool ok() const { return ok_; }
    size_t remaining() const { return pos_ <= size_ ? size_ - pos_ : 0; }

    // Read the CWCP header. Returns the version (2 or 3) or 0, and on
    // success leaves the stream positioned right after the header. For
    // v2 the long double ABI of the writing machine is validated here.
    int peekVersion() {
        char magic[4] = {0};
        if (!raw(magic, 4) || std::memcmp(magic, kMagic, 4) != 0) {
            FileLog::w("BinaryUtil", f_.is_open() ? "Bad magic, treating file as corrupt"
                                                  : "Missing file, treating as empty");
            return 0;
        }
        uint8_t version = 0;
        if (!raw(&version, 1)) return 0;
        if (version > kVersionV3) {
            FileLog::w("BinaryUtil", "Unsupported version " + std::to_string(version));
            return 0;
        }
        if (version == kVersionV3) {
            return version;
        }
        // v2 header layout: [magic][int version][int longDoubleSize]
        // the version int starts one byte before the version byte we read,
        // so back up one byte and take both ints in a single raw read.
        f_.seekg(-1, std::ios::cur);
        pos_ = (pos_ >= 5) ? pos_ - 1 : 0;
        char buf[8] = {0};
        if (!raw(buf, 8)) return 0;
        int longDoubleSize = 0;   // second int of the v2 header
        for (int i = 0; i < 4; i++) {
            longDoubleSize |= static_cast<int>(
                    static_cast<unsigned char>(buf[4 + i])) << (8 * i);   // native LE int, as written
        }
        if (longDoubleSize != static_cast<int>(sizeof(long double))) {
            FileLog::e("BinaryUtil",
                       "Legacy file written by a different long double ABI (" +
                               std::to_string(longDoubleSize) + " vs " +
                               std::to_string(sizeof(long double)) +
                               "), not portable");
            ok_ = false;
            return 0;
        }
        return version;
    }

    bool raw(void *p, size_t n) {
        if (n > remaining()) {
            fail("truncated read");
            return false;
        }
        f_.read(static_cast<char *>(p), n);
        return finish(n);
    }

    bool u8(uint8_t &v) {
        char c;
        if (!raw(&c, 1)) return false;
        v = static_cast<unsigned char>(c);
        return true;
    }

    bool u32(uint32_t &v) {
        char b[4];
        if (!raw(b, 4)) return false;
        v = 0;
        for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(static_cast<unsigned char>(b[i])) << (8 * i);
        return true;
    }

    // Native-endian read of a field that v2 stored as a raw struct member.
    bool nativeUint64(uint64_t &v) {
        return raw(&v, sizeof(v));
    }

    bool i32(int32_t &v) {
        uint32_t u = 0;
        if (!u32(u)) return false;
        v = static_cast<int32_t>(u);
        return true;
    }

    bool f64(double &v) {
        char b[8];
        if (!raw(b, 8)) return false;
        uint64_t bits = 0;
        for (int i = 0; i < 8; i++) bits |= static_cast<uint64_t>(static_cast<unsigned char>(b[i])) << (8 * i);
        std::memcpy(&v, &bits, sizeof(v));
        return true;
    }

    bool string(std::string &s) {
        uint32_t n = 0;
        if (!u32(n)) return false;
        // A string longer than the rest of the file is corrupt data, not
        // memory it can address - reject before allocating.
        if (static_cast<size_t>(n) > remaining()) {
            fail("string longer than remaining file");
            return false;
        }
        s.resize(n);
        return n == 0 || raw(s.data(), n);
    }

    // Element count of a following list. The count is clamped to what the
    // remaining file bytes can hold at minElementSize bytes per element,
    // so a garbage count can only shorten the list, never blow up.
    uint32_t count(int minElementSize) {
        uint32_t n = 0;
        if (!u32(n)) {
            ok_ = false;
            return 0;
        }
        const size_t holdable =
                (minElementSize > 0) ? remaining() / static_cast<size_t>(minElementSize)
                                     : remaining();
        if (n > holdable) {
            FileLog::e("BinaryUtil",
                       "Implausible item count " + std::to_string(n) +
                               " (file only holds " + std::to_string(holdable) +
                               "), clamping");
            n = static_cast<uint32_t>(holdable);
        }
        return n;
    }

    size_t position() const { return pos_; }
    size_t fileSize() const { return size_; }

private:
    bool finish(size_t n) {
        if (f_.gcount() != static_cast<std::streamsize>(n)) {
            fail("short read");
            return false;
        }
        pos_ += n;
        return true;
    }

    void fail(const std::string &reason) {
        if (ok_) FileLog::e("BinaryUtil", "Read error: " + reason);
        ok_ = false;
    }

    std::ifstream f_;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool ok_ = true;
};

// ---------------------------------------------------------------------------
// v3 records
// ---------------------------------------------------------------------------

// Date: wall-clock fields, exactly as they are displayed.
inline void writeDate(Writer &w, const std::tm &tm) {
    w.i32(tm.tm_year + 1900);
    w.u8(static_cast<uint8_t>(tm.tm_mon + 1));
    w.u8(static_cast<uint8_t>(tm.tm_mday));
    w.u8(static_cast<uint8_t>(tm.tm_hour));
    w.u8(static_cast<uint8_t>(tm.tm_min));
    w.u8(static_cast<uint8_t>(tm.tm_sec));
}

inline bool readDate(Reader &r, std::tm &tm) {
    int32_t year = 0;
    uint8_t mon = 0, mday = 0, h = 0, mi = 0, s = 0;
    if (!r.i32(year)) return false;
    if (!r.u8(mon) || !r.u8(mday) || !r.u8(h) || !r.u8(mi) || !r.u8(s)) return false;
    std::memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = mday;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    return true;
}

inline void writeTransaction(Writer &w, const TransactionData &t) {
    w.i32(t.transactionId);
    w.i32(t.walletId);
    w.i32(t.fromWalletId);
    w.i32(t.transactionTypeOrdinal);
    writeDate(w, t.transactionDate);
    w.f64(static_cast<double>(t.amount));
    w.f64(static_cast<double>(t.toAmount));
    w.f64(static_cast<double>(t.nativeAmount));
    w.f64(static_cast<double>(t.amountBonus));
    w.u8(t.isOutsideTransaction ? 1 : 0);
    w.string(t.description);
    w.string(t.currencyType);
    w.string(t.toCurrencyType);
    w.string(t.transactionTypeString);
    w.string(t.transactionHash);
    w.string(t.notes);
}

inline bool readTransaction(Reader &r, TransactionData &t) {
    if (!r.i32(t.transactionId)) return false;
    if (!r.i32(t.walletId)) return false;
    if (!r.i32(t.fromWalletId)) return false;
    if (!r.i32(t.transactionTypeOrdinal)) return false;
    if (!readDate(r, t.transactionDate)) return false;
    double amount = 0, toAmount = 0, nativeAmount = 0, amountBonus = 0;
    if (!r.f64(amount)) return false;
    if (!r.f64(toAmount)) return false;
    if (!r.f64(nativeAmount)) return false;
    if (!r.f64(amountBonus)) return false;
    t.amount = amount;
    t.toAmount = toAmount;
    t.nativeAmount = nativeAmount;
    t.amountBonus = amountBonus;
    uint8_t outside = 0;
    if (!r.u8(outside)) return false;
    t.isOutsideTransaction = outside != 0;
    if (!r.string(t.description)) return false;
    if (!r.string(t.currencyType)) return false;
    if (!r.string(t.toCurrencyType)) return false;
    if (!r.string(t.transactionTypeString)) return false;
    if (!r.string(t.transactionHash)) return false;
    return r.string(t.notes);
}

inline TransactionData toTransactionData(const TransactionStruct &s) {
    TransactionData d;
    d.transactionId = s.transactionId;
    d.walletId = s.walletId;
    d.fromWalletId = s.fromWalletId;
    d.description = s.description;
    d.transactionDate = s.transactionDate;
    d.currencyType = s.currencyType;
    d.toCurrencyType = s.toCurrencyType;
    d.amount = s.amount;
    d.toAmount = s.toAmount;
    d.nativeAmount = s.nativeAmount;
    d.amountBonus = s.amountBonus;
    d.transactionTypeOrdinal = static_cast<int>(s.transactionType);
    d.transactionTypeString = s.transactionTypeString;
    d.transactionHash = s.transactionHash;
    d.isOutsideTransaction = s.isOutsideTransaction;
    d.notes = s.notes;
    return d;
}

inline void writeWallet(Writer &w, const WalletStruct &wlt) {
    w.i32(wlt.walletId);
    w.f64(static_cast<double>(wlt.balance));
    w.f64(static_cast<double>(wlt.nativeBalance));
    w.f64(static_cast<double>(wlt.bonusBalance));
    w.f64(static_cast<double>(wlt.moneySpent));
    w.u8(wlt.isOutsideWallet ? 1 : 0);
    w.string(wlt.currencyType);
    w.string(wlt.notes);
    w.count(wlt.transactions.size());
    for (const auto &tx: wlt.transactions) {
        writeTransaction(w, toTransactionData(tx));
    }
}

// Smallest valid encoded transaction: fixed fields + 6 zero-length strings.
inline constexpr size_t kMinTransactionSize = 16 /*i32x4*/ + 9 /*date*/ + 32 /*f64x4*/ + 1 /*flags*/ + 24 /*string lens*/;
// Smallest valid encoded wallet: fixed fields + 2 empty strings + zero
// transactions. The count clamp must bound the count from ABOVE, so the
// worst case (no transactions) is what keeps it safe.
inline constexpr size_t kMinWalletSize = 4 /*id*/ + 32 /*4xf64*/ + 1 /*flags*/ + 4 + 4 /*2 string lens*/ + 4 /*tx count*/;

inline bool readTransactionBody(Reader &r, TransactionData &t) { return readTransaction(r, t); }

inline bool readWalletBody(Reader &r, WalletStruct &w) {
    int32_t id = 0;
    double balance = 0, nativeBalance = 0, bonusBalance = 0, moneySpent = 0;
    uint8_t flags = 0;
    if (!r.i32(id)) return false;
    if (!r.f64(balance)) return false;
    if (!r.f64(nativeBalance)) return false;
    if (!r.f64(bonusBalance)) return false;
    if (!r.f64(moneySpent)) return false;
    if (!r.u8(flags)) return false;
    w.walletId = id;
    w.balance = balance;
    w.nativeBalance = nativeBalance;
    w.bonusBalance = bonusBalance;
    w.moneySpent = moneySpent;
    w.isOutsideWallet = (flags & 1) != 0;
    if (!r.string(w.currencyType)) return false;
    if (!r.string(w.notes)) return false;
    const uint32_t txCount = r.count(static_cast<int>(kMinTransactionSize));
    w.transactions.reserve(txCount);
    for (uint32_t i = 0; i < txCount; i++) {
        TransactionData tx;
        if (!readTransaction(r, tx)) return false;
        w.transactions.push_back(tx.getTransactionStruct());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Atomic file write: header, callback, then rename .tmp over the target.
// ---------------------------------------------------------------------------
inline bool writeFileV3(const std::string &path, const std::function<bool(Writer &)> &emit) {
    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            FileLog::e("BinaryUtil", "Error opening file for serialization: " + path);
            return false;
        }
        f.write(kMagic, 4);
        const char v = static_cast<char>(kVersionV3);
        f.write(&v, 1);
        Writer w(f);
        if (!emit(w) || !w.ok()) {
            FileLog::e("BinaryUtil", "Serialization failed: " + path);
            return false;
        }
        f.flush();
        if (!f.good()) {
            FileLog::e("BinaryUtil", "Flush failed: " + path);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        FileLog::e("BinaryUtil", "Rename failed for " + path + ": " + ec.message());
        std::filesystem::remove(tmpPath);
        return false;
    }
    return true;
}

inline bool writeWalletStore(const std::string &path, const std::vector<WalletStruct> &in) {
    return writeFileV3(path, [&in](Writer &w) {
        w.count(in.size());
        for (const auto &wallet: in) writeWallet(w, wallet);
        return w.ok();
    });
}

inline bool writeStateFile(const std::string &path, const TransactionManagerState &st) {
    return writeFileV3(path, [&st](Writer &w) {
        w.u8(static_cast<uint8_t>((st.hasTxData ? 1 : 0) | (st.hasCardTxData ? 2 : 0) |
                                         (st.isReadyFlag ? 4 : 0)));
        w.i32(st.txIdCounter);
        w.i32(st.walletIdCounter);
        w.count(st.currencies.size());
        for (const auto &currency: st.currencies) w.string(currency);
        w.count(st.cardTxTypes.size());
        for (const auto &type: st.cardTxTypes) w.string(type);
        return w.ok();
    });
}

// ---------------------------------------------------------------------------
// Legacy v2 files: raw memory dumps of fixed-size C structs (read-only).
// The layouts below are byte-compatible with the structs the old writer
// dumped; do not change field order, types or sizes.
// (Old MagicNumbers: MAX_WALLETS=100, MAX_STRING_LENGTH=100, MAX_DATE_LENGTH=40,
//  MAX_TRANSACTIONS=1000.)
// ---------------------------------------------------------------------------
namespace V2 {

inline constexpr int kMaxWallets = 100;
inline constexpr int kMaxString = 100;
inline constexpr int kMaxDate = 40;
inline constexpr int kMaxTxs = 1000;

struct Transaction {
    int transactionId{};
    int walletId{-1};
    int fromWalletId{};
    char description[kMaxString]{};
    char dateTimeStr[kMaxDate]{};
    char currencyType[kMaxString]{};
    char toCurrencyType[kMaxString]{};
    long double amount{};
    long double toAmount{};
    long double nativeAmount{};
    long double amountBonus{};
    TransactionType transactionType{NONE};
    char transactionTypeString[kMaxString]{};
    char transactionHash[64]{};
    bool isOutsideTransaction{false};
    char notes[255]{};
};

struct Wallet {
    int walletId{};
    Transaction transactions[kMaxTxs]{};
    int numTransactions{};
    char currencyType[kMaxString]{};
    long double balance{};
    long double nativeBalance{};
    long double bonusBalance{};
    long double moneySpent{};
    bool isOutsideWallet{};
    char notes[kMaxString]{};
};

struct State {
    bool isBig{};
    bool hasTxData{};
    bool hasCardTxData{};
    bool isReadyFlag{};
    int txIdCounter{};
    int walletIdCounter{};
    char currencies[kMaxWallets][kMaxString] = {};
    char cardTxTypes[kMaxWallets][kMaxString] = {};
};

inline TransactionStruct convertTransaction(const V2::Transaction &t) {
    TransactionStruct s;
    s.transactionId = t.transactionId;
    s.walletId = t.walletId;
    s.fromWalletId = t.fromWalletId;
    s.description = t.description;
    s.transactionDate = TimestampConverter::stringToTm(t.dateTimeStr);
    s.currencyType = t.currencyType;
    s.toCurrencyType = t.toCurrencyType;
    s.amount = t.amount;
    s.toAmount = t.toAmount;
    s.nativeAmount = t.nativeAmount;
    s.amountBonus = t.amountBonus;
    s.transactionType = t.transactionType;
    s.transactionTypeString = t.transactionTypeString;
    s.transactionHash = t.transactionHash;
    s.isOutsideTransaction = t.isOutsideTransaction;
    s.notes = t.notes;
    return s;
}

inline WalletStruct convertWallet(const V2::Wallet &v) {
    WalletStruct w;
    w.walletId = v.walletId;
    w.currencyType = v.currencyType;
    w.balance = v.balance;
    w.nativeBalance = v.nativeBalance;
    w.bonusBalance = v.bonusBalance;
    w.moneySpent = v.moneySpent;
    w.isOutsideWallet = v.isOutsideWallet;
    w.notes = v.notes;
    int n = v.numTransactions;
    if (n < 0) n = 0;
    if (n > kMaxTxs) {
        FileLog::e("BinaryUtil", "Corrupt numTransactions " + std::to_string(n) +
                                       ", clamping to " + std::to_string(kMaxTxs));
        n = kMaxTxs;
    }
    w.transactions.reserve(n);
    for (int i = 0; i < n; i++) {
        w.transactions.push_back(convertTransaction(v.transactions[i]));
    }
    return w;
}

} // namespace V2

// ---------------------------------------------------------------------------
// File readers (v2 or v3 auto-detect)
// ---------------------------------------------------------------------------

// Returns the version that was read (2/3) or 0 (missing/invalid -> out empty).
inline int readWalletStore(const std::string &path, std::vector<WalletStruct> &out) {
    out.clear();
    Reader r;
    if (!r.open(path)) return 0;
    const int version = r.peekVersion();
    if (version == 0) return 0;
    if (version == kVersionV3) {
        // kMinWalletSize bounds even a wallet with zero transactions, so any
        // real file can hold at least as many wallets as `count` allows.
        const uint32_t count = r.count(static_cast<int>(kMinWalletSize));
        FileLog::d("BinaryUtil", "Reading v3 wallet store " + path);
        for (uint32_t i = 0; i < count; i++) {
            WalletStruct w;
            if (!readWalletBody(r, w)) break;
            out.push_back(std::move(w));
        }
        return version;
    }
    // v2: native u64 count, then raw structs
    uint64_t count64 = 0;
    r.nativeUint64(count64);
    const size_t holdable = r.remaining() / sizeof(V2::Wallet);
    uint64_t count = count64;
    if (count64 > holdable) {
        FileLog::e("BinaryUtil",
                   "Legacy count " + std::to_string(count64) + " exceeds file content " +
                           std::to_string(holdable) + ", clamping");
        count = holdable;
    }
    FileLog::d("BinaryUtil", "Reading legacy v2 wallet store " + path);
    for (uint64_t i = 0; i < count; i++) {
        V2::Wallet v;
        if (!r.raw(&v, sizeof(v))) break;
        out.push_back(V2::convertWallet(v));
    }
    return version;
}

// Returns the version that was read (2/3) or 0 (out left default).
inline int readStateFile(const std::string &path, TransactionManagerState &st) {
    Reader r;
    if (!r.open(path)) return 0;
    const int version = r.peekVersion();
    if (version == 0) return 0;
    if (version == kVersionV3) {
        uint8_t flags = 0;
        if (!r.u8(flags)) return 0;
        st.hasTxData = (flags & 1) != 0;
        st.hasCardTxData = (flags & 2) != 0;
        st.isReadyFlag = (flags & 4) != 0;
        r.i32(st.txIdCounter);
        r.i32(st.walletIdCounter);
        const uint32_t currencyCount = r.count(4);
        st.currencies.reserve(currencyCount);
        for (uint32_t i = 0; i < currencyCount; i++) {
            std::string s;
            if (!r.string(s)) break;
            st.currencies.push_back(std::move(s));
        }
        const uint32_t typeCount = r.count(4);
        st.cardTxTypes.reserve(typeCount);
        for (uint32_t i = 0; i < typeCount; i++) {
            std::string s;
            if (!r.string(s)) break;
            st.cardTxTypes.push_back(std::move(s));
        }
    } else {
        V2::State v;
        if (!r.raw(&v, sizeof(v))) return 0;
        st.hasTxData = v.hasTxData;
        st.hasCardTxData = v.hasCardTxData;
        st.isReadyFlag = v.isReadyFlag;
        st.txIdCounter = v.txIdCounter;
        st.walletIdCounter = v.walletIdCounter;
        for (int i = 0; i < V2::kMaxWallets; i++) {
            if (v.currencies[i][0] == '\0') break;
            st.currencies.emplace_back(v.currencies[i]);
        }
        for (int i = 0; i < V2::kMaxWallets; i++) {
            if (v.cardTxTypes[i][0] == '\0') break;
            st.cardTxTypes.emplace_back(v.cardTxTypes[i]);
        }
    }
    return version;
}

} // namespace BinaryUtil

#endif //NF_TX_CORE_BINARYUTIL_H
