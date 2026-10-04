// Unit tests for the Nf_Tx_Core C++ core.
//
// Build with: cmake -B build -DNF_TX_CORE_BUILD_TESTS=ON
//             cmake --build build
// Run with:   ./build/NF_Tx_Core_Tests   (or ctest --test-dir build)
//
// Build with -fsanitize=address,undefined to also catch memory errors
// (the ASan CI job does this).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "../BinaryUtil.h"
#include "../FileLog.h"
#include "../Price/PriceCache.h"
#include "../Price/StaticPrices.h"
#include "../Structs.h"
#include "../Transaction/BaseTransaction.h"
#include "../TransactionManager.h"
#include "../TransactionParser.h"
#include "../Util/CharUtil.h"
#include "../Util/Util.h"
#include "../Wallet/Wallet.h"

// ---------------------------------------------------------------- harness --
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                       \
    do {                                                                            \
        ++g_checks;                                                                 \
        double _a = (a), _b = (b);                                                  \
        if (_a + (eps) < _b || _a - (eps) > _b) {                                   \
            ++g_failures;                                                           \
            std::printf("FAIL %s:%d: %s (%f) !~ %s (%f)\n", __FILE__, __LINE__,     \
                        #a, _a, #b, _b);                                            \
        }                                                                           \
    } while (0)

#define SECTION(name) std::printf("[ %s ]\n", name)

template <typename F>
void checkThrows(F &&f) {
    bool threw = false;
    try {
        f();
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

// ------------------------------------------------------------- test data ---

static const char *CDC_HEADER =
        "Timestamp (UTC),Transaction Description,Currency,Amount,To Currency,To Amount,"
        "Native Currency,Native Amount,Native Amount (in USD),Transaction Kind,Transaction Hash";

static const char *BOM = "\xEF\xBB\xBF";

// Column layout: ts, desc, cur, amount, toCur, toAmount, natCur, natAmount,
// natUSD, kind, hash (trailing hash left empty)
static std::string cdcLine(const char *ts, const char *cur, const char *amount,
                           const char *nativeAmount, const char *kind,
                           const char *desc = "test") {
    return std::string(ts) + "," + desc + "," + cur + "," + amount + ",,,USD," +
           nativeAmount + "," + nativeAmount + "," + kind + ",";
}

static BaseTransaction parseCdc(const std::string &line) {
    BaseTransaction tx;
    tx.parseCDC(line);
    return tx;
}

static std::unique_ptr<TransactionManager> buildTmFromCdcLines(
        const std::vector<std::string> &lines) {
    auto tm = std::make_unique<TransactionManager>();
    TransactionParser parser(lines);
    parser.parseFromCsv(Mode::CDC);
    tm->setTransactions(parser.getTransactions(), Mode::CDC);
    tm->processTransactions();
    return tm;
}

// BlockPit test helpers
static std::string blockPitHeader() {
    return "Date (UTC);Integration Name;Label;Outgoing Asset;Outgoing Amount;"
           "Incoming Asset;Incoming Amount;Fee Asset (optional);Fee Amount (optional);"
           "Comment (optional);Trx. ID (optional);Source Type;Source Name";
}

// BlockPit line: "DD.MM.YYYY HH:MM:SS;Integration Name;Label;OutAsset;OutAmount;"
//               "InAsset;InAmount;FeeAsset;FeeAmount;Comment;TrxId;SourceType;SourceName"
static std::string bpLine(const char *ts, const char *integration,
                          const char *label, const char *outAsset,
                          const char *outAmount, const char *inAsset,
                          const char *inAmount, const char *feeAsset = "",
                          const char *feeAmount = "", const char *comment = "",
                          const char *trxId = "", const char *sourceType = "API",
                          const char *sourceName = "Bitpanda") {
    return std::string(ts) + ";" + integration + ";" + label + ";" + outAsset + ";" +
           outAmount + ";" + inAsset + ";" + inAmount + ";" + feeAsset + ";" +
           feeAmount + ";" + comment + ";" + trxId + ";" + sourceType + ";" + sourceName;
}

static BaseTransaction parseBlockPit(const std::string &line) {
    BaseTransaction tx;
    tx.parseBlockPit(line);
    return tx;
}

static std::unique_ptr<TransactionManager> buildTmFromBlockPitLines(
        const std::vector<std::string> &lines) {
    auto tm = std::make_unique<TransactionManager>();
    TransactionParser parser(lines);
    parser.parseFromCsv(Mode::BlockPit);
    tm->setTransactions(parser.getTransactions(), Mode::BlockPit);
    tm->processTransactions();
    return tm;
}

// ----------------------------------------------------------------- tests ---

static void testSplitString() {
    SECTION("splitString (legacy)");
    CHECK((splitString("a,b,c", ',') == std::vector<std::string>({"a", "b", "c"})));
    CHECK((splitString("a,b,", ',') == std::vector<std::string>({"a", "b"})));
    CHECK(splitCsvLine("", ',').empty());
    CHECK((splitString("x", ',') == std::vector<std::string>({"x"})));
    CHECK((splitString("a;b", ';') == std::vector<std::string>({"a", "b"})));
}

static void testSplitCsvLine() {
    SECTION("splitCsvLine (RFC-4180)");
    CHECK((splitCsvLine("a,b,c", ',') == std::vector<std::string>({"a", "b", "c"})));
    // quoted field keeps the delimiter
    CHECK((splitCsvLine("a,\"b, c\",d", ',') == std::vector<std::string>({"a", "b, c", "d"})));
    // doubled quotes are an escaped quote
    CHECK((splitCsvLine("\"He said \"\"hi\"\"\",b", ',')
           == std::vector<std::string>({"He said \"hi\"", "b"})));
    // quote not at field start stays literal
    CHECK((splitCsvLine("ab\"cd,e", ',') == std::vector<std::string>({"ab\"cd", "e"})));
    // quoted field containing only delimiter
    CHECK((splitCsvLine("a,\"\",b", ',') == std::vector<std::string>({"a", "", "b"})));
    // trailing delimiter: no extra empty token
    CHECK((splitCsvLine("a,b,", ',') == std::vector<std::string>({"a", "b"})));
    // empty input
    CHECK(splitCsvLine("", ',').empty());
    // custom delimiter
    CHECK((splitCsvLine("a;\"b; c\"", ';') == std::vector<std::string>({"a", "b; c"})));
}

static void testStringToCharArray() {
    SECTION("stringToCharArray (bounds)");
    char exact[4];
    stringToCharArray(exact, sizeof(exact), "abc");
    CHECK(std::string(exact) == "abc");

    char small[4];
    stringToCharArray(small, sizeof(small), "abcdef");
    CHECK(std::string(small) == "abc");          // truncated, NUL terminated
    CHECK('\0' == small[3]);

    char empty[3];
    stringToCharArray(empty, sizeof(empty), "");
    CHECK(std::string(empty) == "");

    // exact fit including NUL
    char fit[3];
    stringToCharArray(fit, sizeof(fit), "ab");
    CHECK(std::string(fit) == "ab");
}

static void testTimestampConverter() {
    SECTION("TimestampConverter");
    const char *s = "2023-04-01 12:34:56";
    auto tm = TimestampConverter::stringToTm(s);
    CHECK(TimestampConverter::tmToString(tm) == std::string(s));
    // fractional seconds are tolerated (Kraken exports)
    auto tm2 = TimestampConverter::stringToTm("2023-06-19 13:34:05.4856");
    CHECK(TimestampConverter::tmToString(tm2) == "2023-06-19 13:34:05");
    checkThrows([] { (void) TimestampConverter::stringToTm("not a date"); });
    checkThrows([] { (void) TimestampConverter::stringToTm("2023-99-99 00:00:00"); });
}

static void testParserHeaders() {
    SECTION("header detection (BOM / CRLF)");
    // plain header is removed
    {
        std::vector<std::string> data{CDC_HEADER, cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase")};
        auto tm = buildTmFromCdcLines(data);
        CHECK(tm->getTransactions().size() == 1);
    }
    // UTF-8 BOM before the header (Windows exports)
    {
        std::vector<std::string> data{BOM + std::string(CDC_HEADER),
                                      cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase")};
        auto tm = buildTmFromCdcLines(data);
        CHECK(tm->getTransactions().size() == 1);
    }
    // CRLF line endings: header and data lines carry a trailing \r
    {
        std::vector<std::string> data{std::string(CDC_HEADER) + "\r",
                                      cdcLine("2023-04-01 12:34:56", "ETH", "2", "50", "crypto_purchase") + "\r"};
        auto tm = buildTmFromCdcLines(data);
        CHECK(tm->getTransactions().size() == 1);
        CHECK(tm->getTransactions()[0].getTransactionTypeString() == "crypto_purchase");
    }
}

static void testParserRobustness() {
    SECTION("malformed lines are skipped and counted");
    {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase"),
                "only,four,columns,here",                       // too short
                cdcLine("2023-04-01 12:34:56", "ETH", "abc", "50", "crypto_purchase"), // bad amount
                cdcLine("2023-04-02 00:00:00", "BTC", "2", "200", "crypto_purchase"),
        };
        TransactionParser parser(data);
        parser.parseFromCsv(Mode::CDC);
        CHECK(parser.getTransactions().size() == 2);
        CHECK(parser.getFailedLines() == 2);
    }
    // quoted description containing a comma stays one field
    {
        const char *line =
                "2023-04-01 12:34:56,\"Buy, then sell\",BTC,1.0,,,USD,100.0,100.0,crypto_purchase,";
        auto tx = parseCdc(line);
        CHECK(tx.getTransactionData().description == "Buy, then sell");
        CHECK(tx.getCurrencyType() == "BTC");
    }
    // Default mode behaves like CDC
    {
        std::vector<std::string> data{CDC_HEADER,
                                      cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase")};
        TransactionParser parser(data);
        parser.parseFromCsv(Mode::Default);
        CHECK(parser.getTransactions().size() == 1);
    }
    // empty data throws
    checkThrows([] {
        std::vector<std::string> empty;
        (void) TransactionParser(empty);
    });
}

static void testParserModes() {
    SECTION("card + kraken parsing");
    // card: 11-column lines, needs at least 8
    {
        std::vector<std::string> data{
                CDC_HEADER,
                "2023-04-01 12:34:56,coffee,EUR,3.20,EUR -> EUR,3.20,USD,3.20,3.20,card_purchase,",
                "2023-04-02 09:00:00,groceries,EUR,50.00,EUR -> EUR,50.00,USD,50.00,50.00,card_purchase,",
        };
        TransactionParser parser(data);
        parser.parseFromCsv(Mode::Card);
        CHECK(parser.getTransactions().size() == 2);
        CHECK(parser.getFailedLines() == 0);
        CHECK(parser.getTransactions()[0].getAmount() == 3.2L);
    }
    // kraken: quoted, comma inside ledgers
    {
        const char *line = R"raw("T67CDX-SB6EI-XIRITS","O3VT22","XXBTZEUR","2023-06-19 13:34:05.4856","buy","limit",24300.00000,49.99992,0.13000,0.00205761,0.00000,"initiated","LUBMNQ-ZAVX6-IGKZMJ,LWLY4J-OZSCV-P4RHND")raw";
        std::vector<std::string> data{
                R"raw("txid","ordertxid","pair","time","type","ordertype","price","cost","fee","vol","margin","misc","ledgers")raw",
                line};
        TransactionParser parser(data);
        parser.parseFromCsv(Mode::Kraken);
        CHECK(parser.getTransactions().size() == 1);
        auto &tx = parser.getTransactions()[0];
        CHECK(tx.getCurrencyType() == "BTC");
        CHECK_NEAR(tx.getAmount(), 0.00205761, 1e-12);   // vol
        CHECK_NEAR(tx.getNativeAmount(), 49.99992, 1e-9); // cost
        // sell flips the sign
        const char *sellLine = R"raw("T0","O0","XXBTZEUR","2023-06-19 13:34:05","sell","limit",24300.0,49.99,0.13,0.002,0.0,"initiated","L1,L2")raw";
        BaseTransaction stx;
        stx.parseKraken(sellLine);
        CHECK(stx.getAmount() < 0);
    }
}

static void testWalletSemantics() {
    SECTION("Wallet add/remove (regression: removal used to wipe the wallet)");
    Wallet wallet("BTC");
    auto t1 = parseCdc(cdcLine("2023-04-01 12:34:56", "BTC", "1.0", "100", "crypto_purchase"));
    auto t2 = parseCdc(cdcLine("2023-04-01 13:00:00", "BTC", "2.0", "200", "crypto_purchase"));
    auto t3 = parseCdc(cdcLine("2023-04-01 14:00:00", "BTC", "3.0", "300", "crypto_purchase"));
    wallet.addTransaction(t1);
    wallet.addTransaction(t2);
    wallet.addTransaction(t3);
    CHECK(wallet.getTransactions().size() == 3);
    CHECK_NEAR(wallet.getBalance(), 6.0, 1e-9);
    CHECK_NEAR(wallet.getMoneySpent(), 600.0, 1e-9);

    wallet.removeTransaction(t2);
    CHECK(wallet.getTransactions().size() == 2);          // exactly one removed
    CHECK_NEAR(wallet.getBalance(), 4.0, 1e-9);
    CHECK_NEAR(wallet.getMoneySpent(), 400.0, 1e-9);

    wallet.removeTransaction(t1);
    CHECK(wallet.getTransactions().size() == 1);
    CHECK_NEAR(wallet.getBalance(), 3.0, 1e-9);
}

static void testIdCounters() {
    SECTION("id counters are forward-only and never throw");
    int before = BaseTransaction::getTxIdCounter();
    BaseTransaction::setTxIdCounter(before + 100);        // raise: allowed
    CHECK(BaseTransaction::getTxIdCounter() == before + 100);
    int high = BaseTransaction::getTxIdCounter();
    BaseTransaction::setTxIdCounter(before);              // lower: must NOT reset
    CHECK(BaseTransaction::getTxIdCounter() == high);

    int walletBefore = Wallet::getWalletIdCounter();
    Wallet::setWalletIdCounter(walletBefore + 50);
    CHECK(Wallet::getWalletIdCounter() == walletBefore + 50);
    int walletHigh = Wallet::getWalletIdCounter();
    Wallet::setWalletIdCounter(0);
    CHECK(Wallet::getWalletIdCounter() == walletHigh);    // stays forward

    // and the next transaction picks up the raised counter
    auto tx = parseCdc(cdcLine("2023-04-01 12:34:56", "BTC", "1", "10", "crypto_purchase"));
    CHECK(tx.getTransactionId() >= high);
}

static void testManagerStates() {
    SECTION("TransactionManager state flags (regression: inverted card condition)");
    {
        // empty manager: no tx data, no card data
        TransactionManager tm;
        auto state = tm.getTransactionManagerState();
        CHECK(!state.hasTxData);
        CHECK(!state.hasCardTxData);
    }
    {
        // crypto-only data
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase"),
                cdcLine("2023-04-02 12:34:56", "ETH", "1", "50", "crypto_purchase")};
        auto tm = buildTmFromCdcLines(data);
        auto state = tm->getTransactionManagerState();
        CHECK(state.hasTxData);
        CHECK(!state.hasCardTxData);
        const auto &currencies = tm->getCurrencies();
        CHECK(currencies.size() >= 2);
    }
    {
        // card-only data: card flags must be true, crypto flags false
        std::vector<std::string> data{
                CDC_HEADER,
                "2023-04-01 12:34:56,coffee,EUR,3.20,EUR -> EUR,3.20,USD,3.20,3.20,card_purchase,"};
        TransactionParser parser(data);
        parser.parseFromCsv(Mode::Card);
        TransactionManager tm;
        tm.setTransactions(parser.getTransactions(), Mode::Card);
        tm.processTransactions();
        auto state = tm.getTransactionManagerState();
        (void) state;
        CHECK(state.hasCardTxData);
        CHECK(!state.hasTxData);
        CHECK(state.cardTxTypes[0][0] != '\0');
    }
}

static void testManagerRoundTrip() {
    SECTION("state round trip + saveData/loadData");
    const std::string dir =
            (std::filesystem::temp_directory_path() / "nf_tx_core_tests").string() + "/";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-04-01 12:34:56", "BTC", "1.5", "150", "crypto_purchase"),
            cdcLine("2023-04-02 12:34:56", "ETH", "2.0", "100", "crypto_purchase")};
    auto tm = buildTmFromCdcLines(data);
    double spentBefore = 0;
    for (const auto &wc: tm->getWallets()) spentBefore += wc.second.getMoneySpent();
    CHECK_NEAR(spentBefore, 250.0, 1e-9);

    // state round trip (the tx id counter is a process-global, forward-only
    // counter, so only the equality against the captured value is testable)
    {
        auto state = tm->getTransactionManagerState();
        CHECK(state.txIdCounter >= 2);
        CHECK(state.hasTxData);
        TransactionManager restored;
        restored.setTransactionManagerState(state);
        auto state2 = restored.getTransactionManagerState();
        CHECK(state2.txIdCounter == state.txIdCounter);
        CHECK(state2.hasTxData);
        CHECK(std::string(state2.currencies[0]) == std::string(state.currencies[0]));
    }

    // binary round trip via saveData/loadData
    tm->saveData(dir);
    CHECK(tm->checkSavedData(dir));
    CHECK(!TransactionManager().checkSavedData(dir + "definitely_missing/"));

    TransactionManager loaded;
    loaded.loadData(dir);
    double spentLoaded = 0;
    for (const auto &wc: loaded.getWallets()) spentLoaded += wc.second.getMoneySpent();
    CHECK_NEAR(spentLoaded, spentBefore, 1e-9);
    CHECK(loaded.getWallets().size() == tm->getWallets().size());
    CHECK(loaded.getTransactions().size() == 2);
    const auto &loadedWallets = loaded.getWallets();
    CHECK(loadedWallets.count("BTC") == 1);
    CHECK_NEAR(loadedWallets.at("BTC").getBalance(), 1.5, 1e-9);

    // prices: setPrices + calculateWalletBalances values the assets
    {
        const auto &curs = tm->getCurrencies();
        std::vector<double> prices(curs.size(), 1.0);
        int btcWalletId = -1;
        for (size_t i = 0; i < curs.size(); i++) {
            if (curs[i] == "BTC") prices[i] = 100.0;
            if (curs[i] == "ETH") prices[i] = 50.0;
        }
        for (const auto &wc: tm->getWallets())
            if (wc.first == "BTC") btcWalletId = wc.second.getWalletId();
        tm->setPrices(prices);
        tm->calculateWalletBalances();
        CHECK_NEAR(tm->getValueOfAssets(btcWalletId), 1.5 * 100.0, 1e-6);
    }

    std::filesystem::remove_all(dir);
}

static void testBinaryUtil() {
    SECTION("binary format v3: portable layout, explicit little-endian");
    const std::string dir =
            (std::filesystem::temp_directory_path() / "nf_tx_core_tests_bin").string() + "/";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // state round trip incl. strings longer than the legacy 100-char limit
    {
        TransactionManagerState st;
        st.hasTxData = true;
        st.hasCardTxData = true;
        st.isReadyFlag = true;
        st.txIdCounter = 424242;
        st.walletIdCounter = 7;
        st.currencies = {"BTC",
                         "ETH",
                         std::string(150, 'a') /* > legacy limit */};
        st.cardTxTypes = {"buy", "sell"};
        CHECK(BinaryUtil::writeStateFile(dir + "state", st));
        TransactionManagerState in;
        in.txIdCounter = -1;
        CHECK(BinaryUtil::readStateFile(dir + "state", in) == 3);
        CHECK(in.hasTxData && in.hasCardTxData && in.isReadyFlag);
        CHECK(in.txIdCounter == 424242);
        CHECK(in.walletIdCounter == 7);
        CHECK(in.currencies == st.currencies);
        CHECK(in.cardTxTypes == st.cardTxTypes);
    }

    // explicit byte layout: CWCP, version 3, little-endian
    {
        TransactionManagerState st;
        st.txIdCounter = 3;
        CHECK(BinaryUtil::writeStateFile(dir + "st2", st));
        std::ifstream f(dir + "st2", std::ios::binary);
        char b[14] = {};
        f.read(b, sizeof b);
        CHECK(f.gcount() == static_cast<std::streamsize>(sizeof b));
        CHECK(std::memcmp(b, "CWCP", 4) == 0);
        CHECK(b[4] == 3);
        CHECK(static_cast<unsigned char>(b[5]) == 0);   // flags
        int32_t txCounter = 0, walletCounter = 0;
        std::memcpy(&txCounter, b + 6, 4);
        std::memcpy(&walletCounter, b + 10, 4);
        CHECK(txCounter == 3);                          // LE int
        CHECK(walletCounter == 0);
    }

    // empty store: header + zero count is a complete file
    {
        CHECK(BinaryUtil::writeWalletStore(dir + "empty", {}));
        CHECK(std::filesystem::file_size(dir + "empty") == 5 + 4);
        std::vector<WalletStruct> in;
        CHECK(BinaryUtil::readWalletStore(dir + "empty", in) == 3);
        CHECK(in.empty());
    }

    // garbage magic / missing file
    {
        std::ofstream f(dir + "garbage", std::ios::binary);
        f.write(std::string(64, 'X').c_str(), 64);
        f.close();
        std::vector<WalletStruct> in;
        CHECK(BinaryUtil::readWalletStore(dir + "garbage", in) == 0);
        CHECK(in.empty());
        CHECK(BinaryUtil::readWalletStore(dir + "no_such_file", in) == 0);
    }

    // corrupt counts are clamped by the remaining file bytes, never fatal
    {
        std::ofstream f(dir + "badcount", std::ios::binary);
        f.write("CWCP", 4);
        f.write("\x03", 1);
        f.write("\xff\xff\xff\xff", 4);   // count = 4 GiB, file has nothing
        f.close();
        std::vector<WalletStruct> in;
        CHECK(BinaryUtil::readWalletStore(dir + "badcount", in) == 3);
        CHECK(in.empty());

        std::ofstream g(dir + "badstring", std::ios::binary);
        g.write("CWCP", 4);
        g.write("\x03", 1);
        g.write("\x01", 1);                    // flags: hasTxData
        g.write("\x01\x00\x00\x00", 4);        // txIdCounter
        g.write("\x00\x00\x00\x00", 4);        // walletIdCounter
        g.write("\xff\xff\xff\x7f", 4);        // currency count = 2^31-1
        g.close();
        TransactionManagerState st;
        st.txIdCounter = -1;
        CHECK(BinaryUtil::readStateFile(dir + "badstring", st) == 3);
        CHECK(st.hasTxData);
        CHECK(st.txIdCounter == 1);
        CHECK(st.currencies.empty());
    }

    // a v2 header from a foreign ABI is rejected (long double mismatch)
    {
        std::ofstream f(dir + "wrong_abi", std::ios::binary);
        f.write("CWCP", 4);
        const int version = 2;
        f.write(reinterpret_cast<const char *>(&version), 4);
        const int wrongSize = static_cast<int>(sizeof(long double)) == 16 ? 8 : 16;
        f.write(reinterpret_cast<const char *>(&wrongSize), 4);
        f.close();
        TransactionManagerState st;
        st.txIdCounter = -1;
        CHECK(BinaryUtil::readStateFile(dir + "wrong_abi", st) == 0);
        CHECK(st.txIdCounter == -1);   // untouched
    }
    std::filesystem::remove_all(dir);
}

static void testUnlimitedPersistence() {
    SECTION("v3 stores hold more wallets/transactions/chars than the legacy caps");
    const std::string dir =
            (std::filesystem::temp_directory_path() / "nf_tx_core_tests_big").string() + "/";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const size_t nWallets = 150;        // legacy cap: 100 wallets
    const size_t nTxsPerWallet = 1200;  // legacy cap: 1000 transactions per wallet
    const std::string longDescription(300, 'x');  // legacy cap: 100 chars
    const std::string longNotes(250, 'n');        // legacy cap: 100 / 255

    std::vector<WalletStruct> in;
    for (size_t i = 0; i < nWallets; i++) {
        WalletStruct w;
        w.walletId = static_cast<int>(i);
        w.currencyType = "CUR" + std::to_string(i % 17);
        w.balance = 1.25 * (i % 11);
        w.nativeBalance = 2.5;
        w.bonusBalance = 0.0;
        w.moneySpent = 0.5;
        w.isOutsideWallet = (i % 2 == 0);
        w.notes = (i == 0) ? longNotes : "note";
        for (size_t j = 0; j < nTxsPerWallet; j++) {
            TransactionStruct t;
            t.transactionId = static_cast<int>(i * nTxsPerWallet + j);
            t.walletId = static_cast<int>(i);
            t.fromWalletId = -1;
            t.description = (i == 0 && j == 0) ? longDescription : "d";
            t.transactionDate = TimestampConverter::stringToTm("2023-05-06 07:08:09");
            t.currencyType = "BTC";
            t.toCurrencyType = "USD";
            t.amount = 1.5;
            t.toAmount = 100.25;
            t.nativeAmount = 100.25;
            t.amountBonus = 0.0;
            t.transactionType = TransactionType::crypto_purchase;
            t.transactionTypeString = "crypto_purchase";
            t.transactionHash = "hash";
            t.isOutsideTransaction = false;
            t.notes = "";
            w.transactions.push_back(std::move(t));
        }
        in.push_back(std::move(w));
    }

    CHECK(BinaryUtil::writeWalletStore(dir + "big", in));

    std::vector<WalletStruct> out;
    CHECK(BinaryUtil::readWalletStore(dir + "big", out) == 3);
    CHECK(out.size() == nWallets);
    CHECK(out[0].transactions.size() == nTxsPerWallet);
    CHECK(out[149].walletId == 149);
    CHECK(out[148].isOutsideWallet);
    CHECK(out[149].isOutsideWallet == false);
    CHECK(out[1].isOutsideWallet == false);
    CHECK(out[0].notes == longNotes);
    CHECK(out[0].transactions[0].description == longDescription);
    CHECK(out[0].transactions[1199].transactionId == 1199);
    CHECK_NEAR(static_cast<double>(out[3].balance), 1.25 * 3, 1e-12);
    CHECK(out[0].transactions[0].transactionDate.tm_hour == 7);

    // compact: no per-wallet 1000-slot slab (the legacy writer needed
    // multiple gigabytes for this data), and a small store is small
    CHECK(std::filesystem::file_size(dir + "big") < 40u * 1024 * 1024);
    {
        std::vector<WalletStruct> one = in;
        one.resize(1);
        CHECK(BinaryUtil::writeWalletStore(dir + "small", one));
        CHECK(std::filesystem::file_size(dir + "small") < 300 * 1024);
    }

    // full TransactionManager pipeline with the legacy caps exceeded
    {
        std::vector<std::string> lines{CDC_HEADER};
        const int nLines = 1100;   // > 1000 transactions, > 100 currencies
        for (int i = 0; i < nLines; i++) {
            char cur[16];
            std::snprintf(cur, sizeof cur, "CUR%03d", i);
            lines.push_back(cdcLine("2023-01-01 00:00:00", cur, "2.0", "200", "crypto_purchase"));
        }
        TransactionParser parser(lines);
        parser.parseFromCsv(Mode::CDC);
        TransactionManager tm;
        tm.setTransactions(parser.getTransactions(), Mode::CDC);
        tm.processTransactions();
        CHECK(tm.getWallets().size() == static_cast<size_t>(nLines));
        CHECK(tm.getCurrencies().size() == static_cast<size_t>(nLines));
        tm.saveData(dir);

        TransactionManager loaded;
        loaded.loadData(dir);
        CHECK(loaded.getWallets().size() == static_cast<size_t>(nLines));
        CHECK(loaded.getCurrencies().size() == static_cast<size_t>(nLines));
        CHECK(loaded.getTransactions().size() == static_cast<size_t>(nLines));
    }
    std::filesystem::remove_all(dir);
}

static void testLegacyV2Migration() {
    SECTION("legacy v2 files stay readable and are upgraded to v3 on save");
    const std::string dir =
            (std::filesystem::temp_directory_path() / "nf_tx_core_tests_legacy").string() + "/";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // synthesize what the old writer produced: header + native u64 + raw structs
    {
        std::ofstream f(dir + "wallets", std::ios::binary | std::ios::trunc);
        f.write(BinaryUtil::kMagic, 4);
        const int version = 2;
        const int lds = static_cast<int>(sizeof(long double));
        f.write(reinterpret_cast<const char *>(&version), 4);
        f.write(reinterpret_cast<const char *>(&lds), 4);
        const uint64_t count = 2;
        f.write(reinterpret_cast<const char *>(&count), 8);

        BinaryUtil::V2::Wallet a{};
        a.walletId = 11;
        std::strcpy(a.currencyType, "BTC");
        a.numTransactions = 1;
        a.balance = 3.5L;
        std::strcpy(a.notes, "legacy");
        auto &t = a.transactions[0];
        t.transactionId = 5;
        t.walletId = 11;
        std::strcpy(t.description, "coffee");
        std::strcpy(t.dateTimeStr, "2023-04-01 12:34:56");
        std::strcpy(t.currencyType, "EUR");
        std::strcpy(t.toCurrencyType, "USD");
        t.amount = 3.2L;
        t.nativeAmount = 3.2L;
        t.transactionType = TransactionType::crypto_purchase;
        std::strcpy(t.transactionTypeString, "crypto_purchase");

        BinaryUtil::V2::Wallet b{};
        b.walletId = 12;
        std::strcpy(b.currencyType, "ETH");

        f.write(reinterpret_cast<const char *>(&a), sizeof a);
        f.write(reinterpret_cast<const char *>(&b), sizeof b);
    }
    {
        std::ofstream f(dir + "state", std::ios::binary | std::ios::trunc);
        f.write(BinaryUtil::kMagic, 4);
        const int version = 2;
        const int lds = static_cast<int>(sizeof(long double));
        f.write(reinterpret_cast<const char *>(&version), 4);
        f.write(reinterpret_cast<const char *>(&lds), 4);
        BinaryUtil::V2::State s{};
        s.hasTxData = true;
        s.txIdCounter = 100;
        s.walletIdCounter = 200;
        std::strcpy(s.currencies[0], "BTC");
        std::strcpy(s.currencies[1], "ETH");
        f.write(reinterpret_cast<const char *>(&s), sizeof s);
    }
    // cardWallets: empty v2 store
    {
        std::ofstream f(dir + "cardWallets", std::ios::binary | std::ios::trunc);
        f.write(BinaryUtil::kMagic, 4);
        const int version = 2;
        const int lds = static_cast<int>(sizeof(long double));
        f.write(reinterpret_cast<const char *>(&version), 4);
        f.write(reinterpret_cast<const char *>(&lds), 4);
        const uint64_t count = 0;
        f.write(reinterpret_cast<const char *>(&count), 8);
    }

    // direct reader checks
    std::vector<WalletStruct> in;
    CHECK(BinaryUtil::readWalletStore(dir + "wallets", in) == 2);
    CHECK(in.size() == 2);
    CHECK(in[0].walletId == 11);
    CHECK(in[0].currencyType == "BTC");
    CHECK(in[0].notes == "legacy");
    CHECK_NEAR(static_cast<double>(in[0].balance), 3.5, 1e-12);
    CHECK(in[0].transactions.size() == 1);
    CHECK(in[0].transactions[0].description == "coffee");
    CHECK(in[0].transactions[0].currencyType == "EUR");
    CHECK(in[0].transactions[0].transactionDate.tm_hour == 12);
    CHECK(in[1].currencyType == "ETH");

    // full pipeline: load the legacy dir, re-save -> v3, re-load -> identical
    TransactionManager tm;
    tm.loadData(dir);
    CHECK(tm.getTransactionManagerState().hasTxData);
    CHECK(tm.getWallets().size() == 2);
    CHECK(tm.getCurrencies().size() == 2);
    CHECK(tm.getTransactions().size() == 1);
    const double spent = tm.getWallets().at("BTC").getMoneySpent();

    tm.saveData(dir);

    // the files are now v3
    for (const char *name: {"wallets", "state", "cardWallets"}) {
        std::ifstream f(dir + name, std::ios::binary);
        char hb[5] = {};
        f.read(hb, 5);
        CHECK(std::memcmp(hb, "CWCP", 4) == 0);
        CHECK(hb[4] == 3);
    }

    TransactionManager loaded;
    loaded.loadData(dir);
    CHECK(loaded.getWallets().size() == 2);
    CHECK(loaded.getTransactions().size() == 1);
    CHECK_NEAR(static_cast<double>(loaded.getWallets().at("BTC").getMoneySpent()), spent, 1e-12);
    {
        Wallet btcWallet = loaded.getWallets().at("BTC");
        const auto txs = btcWallet.getTransactions();
        CHECK(!txs.empty() && txs[0].getTransactionData().description == "coffee");
    }

    std::filesystem::remove_all(dir);
}

static void testBlockPitParser() {
    SECTION("BlockPit header detection");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda Depot", "Airdrop", "", "", "BTC", "0.5")};
        TransactionParser parser(data);
        parser.parseFromCsv(Mode::BlockPit);
        CHECK(parser.getTransactions().size() == 1);
    }

    SECTION("Timestamp parsing");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Bitpanda;Airdrop;;;BTC;0.5");
        CHECK(TimestampConverter::tmToString(tx.getTransactionData().transactionDate) == "2026-10-01 21:30:23");
    }

    SECTION("Airdrop parsing");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Bitpanda;Airdrop;;;BTC;0.50000");
        CHECK(tx.getCurrencyType() == "BTC");
        CHECK_NEAR(tx.getAmount(), 0.5, 1e-6);
        CHECK(tx.getTransactionType() == crypto_airdrop_credited);
        CHECK(tx.getTransactionTypeString() == "airdrop");
    }

    SECTION("Staking parsing");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Bitpanda;Staking;;;ETH;1.234");
        CHECK(tx.getCurrencyType() == "ETH");
        CHECK_NEAR(tx.getAmount(), 1.234, 1e-6);
        CHECK(tx.getTransactionType() == crypto_earn_interest_paid);
    }

    SECTION("Trade (EUR -> BTC) parsing");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Bitpanda;Trade;EUR;499.99;BTC;0.00205761;EUR;0.99;Buy BTC;");
        CHECK(tx.getCurrencyType() == "BTC");
        CHECK_NEAR(tx.getAmount(), 0.00205761, 1e-8);
        CHECK(tx.getToCurrencyType() == "EUR");
        CHECK_NEAR(tx.getToAmount(), 499.99, 1e-6);
        CHECK(tx.getTransactionType() == crypto_purchase);
        CHECK_NEAR(tx.getFeeAmount(), 0.99, 1e-9);
        CHECK(tx.getFeeAsset() == "EUR");
    }

    SECTION("Trade (BTC -> EUR) sale parsing");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Bitpanda;Trade;BTC;0.00205761;EUR;499.99;;;Sell BTC;");
        CHECK(tx.getCurrencyType() == "BTC");
        CHECK(tx.getTransactionType() == crypto_purchase);
        // Sale: the crypto wallet is debited, the fiat proceeds are negative
        // native so the crypto wallet's moneySpent is reduced.
        CHECK_NEAR(tx.getAmount(), -0.00205761, 1e-8);
        CHECK(tx.getToCurrencyType() == "EUR");
        CHECK_NEAR(tx.getToAmount(), 499.99, 1e-6);
        CHECK_NEAR(tx.getNativeAmount(), -499.99, 1e-6);
    }

    SECTION("Deposit parsing");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Kraken;Deposit;;;XRP;100.00");
        CHECK(tx.getCurrencyType() == "XRP");
        CHECK_NEAR(tx.getAmount(), 100.0, 1e-6);
        CHECK(tx.getTransactionType() == crypto_deposit);
    }

    SECTION("Withdrawal parsing");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Kraken;Withdrawal;BTC;0.001;;;0");
        CHECK(tx.getCurrencyType() == "BTC");
        CHECK(tx.getTransactionType() == crypto_withdrawal);
    }

    SECTION("Fee handling");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Bitpanda;Trade;EUR;500;BTC;0.01;EUR;2.50;Trade;");
        CHECK_NEAR(tx.getFeeAmount(), 2.50, 1e-6);
        CHECK(tx.getFeeAsset() == "EUR");
    }

    SECTION("Empty optional fields");
    {
        auto tx = parseBlockPit("01.10.2026 21:30:23;Bitpanda;Interest;;;SOL;5.0");
        CHECK(tx.getCurrencyType() == "SOL");
        CHECK_NEAR(tx.getAmount(), 5.0, 1e-6);
        CHECK(tx.getTransactionType() == crypto_earn_interest_paid);
    }
}

static void testBlockPitManager() {
    SECTION("BlockPit airdrop creates wallet and sets bonus");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Airdrop", "", "", "BTC", "0.5")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK(tm->getWallets().count("BTC") == 1);
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), 0.5, 1e-6);
        // Airdrop should set amount to bonus
        auto &wallet = tm->getWallets().at("BTC");
        CHECK(wallet.getBonusBalance() > 0);
    }

    SECTION("BlockPit trade: crypto credited, fiat leaves the outside wallet");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Trade", "EUR", "500", "BTC", "0.01")};
        auto tm = buildTmFromBlockPitLines(data);
        // BTC wallet is credited with the purchased quantity
        CHECK(tm->getWallets().count("BTC") == 1);
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), 0.01, 1e-9);
        // The fiat cost is tracked on the crypto wallet (like the CDC parser)
        CHECK_NEAR(tm->getWallets().at("BTC").getMoneySpent(), 500.0, 1e-9);
        // The fiat side is an outside wallet, not a regular crypto wallet
        CHECK(tm->getWallets().count("EUR") == 0);
        CHECK(tm->getOutWallets().count("EUR") == 1);
        CHECK_NEAR(tm->getOutWallets().at("EUR").getBalance(), -500.0, 1e-9);
    }

    SECTION("BlockPit sale: crypto debited, fiat proceeds credit the outside wallet");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Trade", "BTC", "0.01", "EUR", "500")};
        auto tm = buildTmFromBlockPitLines(data);
        // The BTC wallet is debited; no phantom inner EUR wallet is created
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), -0.01, 1e-9);
        CHECK(tm->getWallets().count("EUR") == 0);
        // The sale reduces the crypto wallet's moneySpent
        CHECK_NEAR(tm->getWallets().at("BTC").getMoneySpent(), -500.0, 1e-9);
        // The fiat proceeds sit in the outside EUR wallet with a positive balance
        CHECK(tm->getOutWallets().count("EUR") == 1);
        CHECK_NEAR(tm->getOutWallets().at("EUR").getBalance(), 500.0, 1e-9);
    }

    SECTION("BlockPit withdrawal debits the asset wallet and credits outside");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Kraken", "Deposit", "", "", "BTC", "0.01"),
                                      bpLine("02.10.2026 21:30:23", "Kraken", "Withdrawal", "BTC", "0.001", "", "")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), 0.009, 1e-9);
        CHECK_NEAR(tm->getOutWallets().at("BTC").getBalance(), -0.009, 1e-9);
    }

    SECTION("BlockPit Non-Taxable Out debits instead of inflating the balance");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Kraken", "Non-Taxable In", "", "", "LTC", "0.01"),
                                      bpLine("02.10.2026 21:30:23", "Kraken", "Non-Taxable Out", "LTC", "0.002", "", "")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK_NEAR(tm->getWallets().at("LTC").getBalance(), 0.008, 1e-9);
    }

    SECTION("BlockPit swap splits into credit and debit on both wallets");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Trade", "DOGE", "10", "ETH", "0.5")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK_NEAR(tm->getWallets().at("ETH").getBalance(), 0.5, 1e-9);
        CHECK_NEAR(tm->getWallets().at("DOGE").getBalance(), -10.0, 1e-9);
        // Credit and debit are both stored, so both survive the save format
        CHECK(tm->getWallets().at("ETH").getTransactions().size() == 1);
        CHECK(tm->getWallets().at("DOGE").getTransactions().size() == 1);
        auto dogeTxs = tm->getWallets().at("DOGE").getTransactions();
        const auto &debit = dogeTxs.front();
        CHECK_NEAR(debit.getAmount(), -10.0, 1e-9);
        CHECK(debit.getCurrencyType() == "DOGE");
    }

    SECTION("BlockPit fee and lost labels debit the asset wallet");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Interest", "", "", "KFEE", "100"),
                                      bpLine("02.10.2026 21:30:23", "Bitpanda", "Fee", "KFEE", "28.51", "", "", "KFEE", "28.51"),
                                      bpLine("03.10.2026 21:30:23", "Bitpanda", "Lost", "KFEE", "5", "", "")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK_NEAR(tm->getWallets().at("KFEE").getBalance(), 100 - 28.51 - 5, 1e-9);
    }

    SECTION("BlockPit: every transaction ends up in a wallet (no dangling ids)");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Trade", "EUR", "500", "BTC", "0.01"),
                                      bpLine("02.10.2026 21:30:23", "Bitpanda", "Trade", "DOGE", "10", "ETH", "0.5"),
                                      bpLine("03.10.2026 21:30:23", "Kraken", "Withdrawal", "BTC", "0.001", "", ""),
                                      bpLine("04.10.2026 21:30:23", "Kraken", "Lost", "DOGE", "1", "", ""),
                                      bpLine("05.10.2026 21:30:23", "Bitpanda", "Mystery Label", "", "", "SOL", "2")};
        auto tm = buildTmFromBlockPitLines(data);
        const auto &wallets = tm->getWallets();
        const auto &outside = tm->getOutWallets();
        std::set<int> ids;
        for (const auto &pair: wallets) ids.insert(pair.second.getWalletId());
        for (const auto &pair: outside) ids.insert(pair.second.getWalletId());
        for (const auto &tx: tm->getTransactions()) {
            CHECK(ids.count(tx.getWalletId()) == 1);
        }
    }

    SECTION("BlockPit: types survive a save/load round trip");
    {
        const std::string dir =
                (std::filesystem::temp_directory_path() / "nf_tx_core_tests").string() + "/bp_roundtrip/";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Airdrop", "", "", "BTC", "0.5"),
                                      bpLine("02.10.2026 21:30:23", "Kraken", "Non-Taxable In", "", "", "LTC", "1")};
        auto tm = buildTmFromBlockPitLines(data);
        tm->saveData(dir);
        TransactionManager restored;
        restored.loadData(dir);
        TransactionType first = NONE, second = NONE;
        for (const auto &tx: restored.getTransactions()) {
            if (tx.getCurrencyType() == "BTC") first = tx.getTransactionType();
            if (tx.getCurrencyType() == "LTC") second = tx.getTransactionType();
        }
        CHECK(first == crypto_airdrop_credited);
        CHECK(second == crypto_transfer);
        std::filesystem::remove_all(dir);
    }

    SECTION("BlockPit interest creates wallet");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Interest", "", "", "ETH", "1.5")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK(tm->getWallets().count("ETH") == 1);
        CHECK_NEAR(tm->getWallets().at("ETH").getBalance(), 1.5, 1e-6);
    }

    SECTION("Multiple transaction types");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Airdrop", "", "", "BTC", "0.5"),
                                      bpLine("02.10.2026 21:30:23", "Bitpanda", "Interest", "", "", "ETH", "1.5"),
                                      bpLine("03.10.2026 21:30:23", "Kraken", "Deposit", "", "", "XRP", "100")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK(tm->getWallets().count("BTC") == 1);
        CHECK(tm->getWallets().count("ETH") == 1);
        CHECK(tm->getWallets().count("XRP") == 1);
        CHECK(tm->getTransactions().size() == 3);
    }
}

static void testBlockPitTimestampParsing() {
    SECTION("DD.MM.YYYY HH:MM:SS format");
    {
        auto tm1 = TimestampConverter::stringToTmBlockPit("01.12.2023 10:30:45");
        CHECK(tm1.tm_mday == 1);
        CHECK(tm1.tm_mon == 11);  // 0-indexed (December = 11)
        CHECK(tm1.tm_year == 123); // 0-indexed (2023 - 1900 = 123)
        CHECK(tm1.tm_hour == 10);
        CHECK(tm1.tm_min == 30);
        CHECK(tm1.tm_sec == 45);
    }

    SECTION("Edge: single digit day/month");
    {
        auto tm1 = TimestampConverter::stringToTmBlockPit("1.5.2023 09:00:00");
        CHECK(tm1.tm_mday == 1);
        CHECK(tm1.tm_mon == 4);   // May
        CHECK(tm1.tm_year == 123);
    }

    SECTION("Empty string throws");
    {
        checkThrows([] { (void) TimestampConverter::stringToTmBlockPit(""); });
    }
}

static void testCdcWalletSignRegression() {
    // Pins the signed-amount convention of the CDC/Kraken modes: CSV amounts
    // are signed, wallets always apply them with addTransaction.
    SECTION("CDC withdrawal debits the asset wallet, credits outside");
    {
        std::vector<std::string> data{CDC_HEADER,
                                      cdcLine("2023-04-01 12:34:56", "BTC", "-1", "-100", "crypto_withdrawal")};
        auto tm = buildTmFromCdcLines(data);
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), -1.0, 1e-9);
        CHECK_NEAR(tm->getOutWallets().at("BTC").getBalance(), 1.0, 1e-9);
        CHECK_NEAR(tm->getWallets().at("BTC").getMoneySpent(), -100.0, 1e-9);
    }

    SECTION("CDC swap debited is applied as a plain addition of the signed amount");
    {
        std::vector<std::string> data{CDC_HEADER,
                                      cdcLine("2023-04-01 12:34:56", "ETH", "-2", "-200", "crypto_wallet_swap_debited")};
        auto tm = buildTmFromCdcLines(data);
        CHECK_NEAR(tm->getWallets().at("ETH").getBalance(), -2.0, 1e-9);
        CHECK_NEAR(tm->getWallets().at("ETH").getBonusBalance(), -2.0, 1e-9);
    }
}

static void testPriceCache() {
    SECTION("PriceCache replaces entries (regression: stale prices)");
    PriceCache cache;
    cache.addPrice("BTC", 100.0);
    CHECK_NEAR(cache.checkCache("BTC"), 100.0, 1e-9);
    cache.addPrice("BTC", 200.0);
    CHECK_NEAR(cache.checkCache("BTC"), 200.0, 1e-9);       // replaced, not stale
    CHECK(cache.testCache("BTC"));
    CHECK(!cache.testCache("NOPE"));
    CHECK_NEAR(cache.checkCache("NOPE"), -1.0, 1e-9);   // "unknown" sentinel

    StaticPrices staticPrices;
    CHECK(staticPrices.prices.count("BTC") == 1);
    CHECK(staticPrices.prices.at("BTC") > 0);
}

static void testXmlSerialization() {
    SECTION("serializeToXml (ASan: dangling node names would abort here)");
    TransactionData tx;
    tx.transactionId = 7;
    tx.transactionDate = TimestampConverter::stringToTm("2023-04-01 12:34:56");
    tx.description = "desc";
    tx.currencyType = "BTC";
    tx.transactionTypeString = "crypto_purchase";
    std::string xml = tx.serializeToXml();
    CHECK(xml.find("BTC") != std::string::npos);
    CHECK(xml.find("crypto_purchase") != std::string::npos);
    CHECK(xml.find("2023-04-01 12:34:56") != std::string::npos);

    WalletData wallet;
    wallet.walletId = 3;
    wallet.currencyType = "ETH";
    std::string walletXml = wallet.serializeToXml();
    CHECK(walletXml.find("ETH") != std::string::npos);
}

static void testFileLogLevels() {
    SECTION("FileLog::setMaxLogLevel (regression: setter was a no-op)");
    FileLog::setMaxLogLevel(3);     // was inverted-bounds / self-assign
    FileLog::setMaxLogLevel(0);
    FileLog::v("t", "v message must not crash");
    FileLog::d("t", "d message");
    FileLog::i("t", "i message");
    CHECK(true);
}

static void testReferencedQuietWalletKept() {
    // supercharger_deposit is a type that addCDCTransactionsToWallets
    // deliberately does not add to the wallet's own list. The stored
    // transaction still references that wallet, so removeEmptyWallets must
    // keep it - otherwise the id dangles (Room FK violation, broken saves).
    auto tm = buildTmFromCdcLines(
        {cdcLine("2023-05-01 12:00:00", "SC", "0.5", "5.0", "supercharger_deposit")});
    int wid = -1;
    for (const auto &entry: tm->getWallets()) {
        if (entry.second.getCurrencyType() == "SC") wid = entry.second.getWalletId();
    }
    CHECK(wid != -1);
    CHECK(!tm->getTransactions().empty()
          && tm->getTransactions().front().getWalletId() == wid);
}

// ------------------------------------------------------------------ main ---

int main() {
    FileLog::init("tests.log", true, FileLog::LOG_ERROR);   // quiet: only errors go to file

    testSplitString();
    testSplitCsvLine();
    testStringToCharArray();
    testTimestampConverter();
    testBlockPitTimestampParsing();
    testParserHeaders();
    testParserRobustness();
    testParserModes();
    testWalletSemantics();
    testIdCounters();
    testManagerStates();
    testReferencedQuietWalletKept();
    testBlockPitParser();
    testBlockPitManager();
    testCdcWalletSignRegression();
    testManagerRoundTrip();
    testBinaryUtil();
    testUnlimitedPersistence();
    testLegacyV2Migration();
    testPriceCache();
    testXmlSerialization();
    testFileLogLevels();

    std::printf("\n%d checks, %d failure%s\n", g_checks, g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
