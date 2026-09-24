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
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../BinaryUtil.h"
#include "../FileLog.h"
#include "../Price/PriceCache.h"
#include "../Price/StaticPrices.h"
#include "../Structs.h"
#include "../Transaction/BaseTransaction.h"
#include "../TransactionManager/TMState.h"
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
    SECTION("BinaryUtil header validation (corrupt / old formats are safe)");
    const std::string dir =
            (std::filesystem::temp_directory_path() / "nf_tx_core_tests_bin").string() + "/";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // round trip of a TMState
    {
        TMState out;
        out.txIdCounter = 42;
        out.hasTxData = true;
        std::strcpy(out.currencies[0], "BTC");
        BinaryUtil::serializeStruct(out, dir + "state_ok");
        TMState in;
        BinaryUtil::deserializeStruct(in, dir + "state_ok");
        CHECK(in.txIdCounter == 42);
        CHECK(in.hasTxData);
        CHECK(std::string(in.currencies[0]) == "BTC");
    }
    // garbage file -> zeroed state, no crash
    {
        std::ofstream f(dir + "state_garbage", std::ios::binary);
        f.write(std::string(64, 'X').c_str(), 64);
        f.close();
        TMState in;
        in.txIdCounter = 7;                       // must be zeroed
        BinaryUtil::deserializeStruct(in, dir + "state_garbage");
        CHECK(in.txIdCounter == 0);
        CHECK(!in.hasTxData);
    }
    // old format (no header) -> rejected
    {
        TMState raw;
        raw.txIdCounter = 9;
        // write the struct body WITHOUT a header, as pre-v2 files did
        std::ofstream f(dir + "state_old", std::ios::binary);
        f.write(reinterpret_cast<const char *>(&raw), sizeof(TMState));
        f.close();
        TMState in;
        in.txIdCounter = 7;
        BinaryUtil::deserializeStruct(in, dir + "state_old");
        CHECK(in.txIdCounter == 0);
    }
    // header with wrong long double size -> rejected
    {
        std::ofstream f(dir + "state_arch", std::ios::binary);
        BinaryUtil::FileHeader h;
        std::strcpy(h.magic, "CWCP");
        h.version = 2;
        h.longDoubleSize = sizeof(long double) == 16 ? 8 : 16;     // wrong size
        f.write(reinterpret_cast<const char *>(&h), sizeof(h));
        f.close();
        TMState in;
        in.txIdCounter = 7;
        BinaryUtil::deserializeStruct(in, dir + "state_arch");
        CHECK(in.txIdCounter == 0);
    }
    std::filesystem::remove_all(dir);
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

// ------------------------------------------------------------------ main ---

int main() {
    FileLog::init("tests.log", true, 1);   // quiet: errors only go to file

    testSplitString();
    testSplitCsvLine();
    testStringToCharArray();
    testTimestampConverter();
    testParserHeaders();
    testParserRobustness();
    testParserModes();
    testWalletSemantics();
    testIdCounters();
    testManagerStates();
    testManagerRoundTrip();
    testBinaryUtil();
    testPriceCache();
    testXmlSerialization();
    testFileLogLevels();

    std::printf("\n%d checks, %d failure%s\n", g_checks, g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
