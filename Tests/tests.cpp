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
#include <ctime>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../BalanceSheet.h"
#include "../BinaryUtil.h"
#include "../FileLog.h"
#include "../library.h"
#include "../Price/AssetValue.h"
#include "../Price/PriceCache.h"
#include "../Price/StaticPrices.h"
#include "../Structs.h"
#include "../Transaction/BaseTransaction.h"
#include "../TransactionManager.h"
#include "../TransactionParser.h"
#include "../Util/CharUtil.h"
#include "../Util/Util.h"
#include "../DataHolder.h"
#include "../ExportCsv.h"
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

// Committed synthetic fixtures in Tests/fixtures/ (path via
// NF_TX_CORE_TEST_DATA_DIR, set by CMake): derived from the shape of real
// exports (headers, labels, fee layout, BlockPit CRLF endings) with
// synthetic values - the real data files stay out of git.
static std::vector<std::string> loadFixture(const char *name) {
    std::vector<std::string> lines;
    std::ifstream f(std::filesystem::path(NF_TX_CORE_TEST_DATA_DIR) / name);
    CHECK(f.good());
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);
    return lines;
}

// XML round-trip helpers for the C set*Data surface (the exact format the
// app's Firebase restore ships: WalletData/TransactionData serialized to
// XML strings, deserialized by library.cpp).
static std::string walletDataXml(int id, const std::string &cur, double balance) {
    WalletData w;
    w.walletId = id;
    w.currencyType = cur;
    w.balance = balance;
    w.nativeBalance = balance;
    return w.serializeToXml();
}

static std::string txDataXml(int id, const std::string &cur, long double amount) {
    TransactionData t;
    t.transactionId = id;
    t.currencyType = cur;
    t.amount = amount;
    t.nativeAmount = amount;
    return t.serializeToXml();
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

    // Save into a nested directory that does not exist yet: the writer
    // must create it (Android never pre-creates the save directory).
    const std::string nested = dir + "nested/does/not/exist/";
    std::filesystem::remove_all(nested);
    CHECK(tm->saveData(nested));
    CHECK(tm->checkSavedData(nested));

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

// The "careful when loading from DB" pin: a relaunched process starts the
// process-global id counter at 0, so only the counter persisted in the v3
// state blob keeps a restored core from re-issuing saved ids. This walks the
// exact load seam (import -> saveData -> fresh process -> loadData -> new
// import) and asserts the fresh import's ids continue strictly above the
// saved ones, monotonic and without duplicates.
// The guarded setter deliberately refuses backwards moves, so the emulation
// of the process start writes the global directly.
extern int txIdCounter;

static void testTxIdCounterRoundTrip() {
    SECTION("txIdCounter round trip: no id reuse after state restore");
    const std::string dir = (std::filesystem::temp_directory_path() /
                             "nf_tx_core_txid_roundtrip").string() + "/";
    std::filesystem::remove_all(dir);

    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-06-01 10:00:00", "BTC", "1.0", "10", "crypto_purchase"),
            cdcLine("2023-06-01 11:00:00", "BTC", "2.0", "20", "crypto_purchase"),
            cdcLine("2023-06-02 10:00:00", "ETH", "1.0", "10", "crypto_purchase"),
            cdcLine("2023-06-03 10:00:00", "BTC", "3.0", "30", "crypto_purchase"),
            cdcLine("2023-06-04 10:00:00", "ETH", "1.0", "10", "crypto_purchase")};
    auto tm = buildTmFromCdcLines(data);
    CHECK(tm->getTransactions().size() == 5);
    const int first = tm->getTransactions().front().getTransactionId();
    const int last = tm->getTransactions().back().getTransactionId();
    CHECK(last - first == 4);                           // contiguous within the batch
    CHECK(BaseTransaction::getTxIdCounter() == last + 1);  // counter sits past the batch
    CHECK(tm->saveData(dir));

    std::set<int> savedIds;
    for (const auto &tx: tm->getTransactions()) savedIds.insert(tx.getTransactionId());

    // Fresh process: the counter starts at 0 (see the comment above).
    txIdCounter = 0;
    CHECK(BaseTransaction::getTxIdCounter() == 0);

    TransactionManager loaded;
    loaded.loadData(dir);
    CHECK(BaseTransaction::getTxIdCounter() == last + 1);   // restored by the blob, not 0
    CHECK(loaded.getTransactions().size() == 5);

    // A new import on top of the restored state: ids unique, monotonic, and
    // strictly above every saved id (no cross-import duplicates).
    std::vector<std::string> data2{
            CDC_HEADER,
            cdcLine("2023-06-05 10:00:00", "BTC", "1.0", "10", "crypto_purchase"),
            cdcLine("2023-06-06 10:00:00", "ETH", "2.0", "20", "crypto_purchase")};
    TransactionParser parser2(data2);
    parser2.parseFromCsv(Mode::CDC);
    auto fresh = parser2.getTransactions();
    CHECK(fresh.size() == 2);
    for (size_t i = 0; i < fresh.size(); ++i) {
        CHECK(fresh[i].getTransactionId() > last);
        CHECK(savedIds.count(fresh[i].getTransactionId()) == 0);
        if (i > 0) {
            CHECK(fresh[i].getTransactionId() > fresh[i - 1].getTransactionId());
        }
    }

    // A second fresh import in the same process must not collide either.
    std::vector<std::string> data3{
            CDC_HEADER,
            cdcLine("2023-06-07 10:00:00", "BTC", "1.0", "10", "crypto_purchase")};
    TransactionParser parser3(data3);
    parser3.parseFromCsv(Mode::CDC);
    auto second = parser3.getTransactions();
    CHECK(second.size() == 1);
    CHECK(second.front().getTransactionId() > fresh.back().getTransactionId());
    CHECK(savedIds.count(second.front().getTransactionId()) == 0);

    std::filesystem::remove_all(dir);
}

static void testBinaryUtil() {
    SECTION("binary format (v5): portable layout, explicit little-endian");
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
        // The additive v5 section: the user-owned sheet text rides the
        // v5 state file and round-trips byte-exact.
        st.balanceSheetText = "L;id1;Home\u00a6loan;150000;3.575;2019-02-01;-;-";
        CHECK(BinaryUtil::writeStateFile(dir + "state", st));
        TransactionManagerState in;
        in.txIdCounter = -1;
        CHECK(BinaryUtil::readStateFile(dir + "state", in) == BinaryUtil::kVersionV5);
        CHECK(in.hasTxData && in.hasCardTxData && in.isReadyFlag);
        CHECK(in.txIdCounter == 424242);
        CHECK(in.walletIdCounter == 7);
        CHECK(in.currencies == st.currencies);
        CHECK(in.cardTxTypes == st.cardTxTypes);
        CHECK(in.balanceSheetText == st.balanceSheetText);
    }

    // explicit byte layout: CWCP, version 5, little-endian
    {
        TransactionManagerState st;
        st.txIdCounter = 3;
        CHECK(BinaryUtil::writeStateFile(dir + "st2", st));
        std::ifstream f(dir + "st2", std::ios::binary);
        char b[14] = {};
        f.read(b, sizeof b);
        CHECK(f.gcount() == static_cast<std::streamsize>(sizeof b));
        CHECK(std::memcmp(b, "CWCP", 4) == 0);
        CHECK(b[4] == BinaryUtil::kVersionV5);
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
        CHECK(BinaryUtil::readWalletStore(dir + "empty", in) == BinaryUtil::kVersionV5);
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
        // The crafted header says v3: the reader honours it (layout v3),
        // clamps the count to zero, and reports the file's version.
        CHECK(BinaryUtil::readWalletStore(dir + "badcount", in) == BinaryUtil::kVersionV3);
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
        // Crafted v3 header: the reader honours the version byte.
        CHECK(BinaryUtil::readStateFile(dir + "badstring", st) == BinaryUtil::kVersionV3);
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
    SECTION("v4 stores hold more wallets/transactions/chars than the legacy caps");
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
    CHECK(BinaryUtil::readWalletStore(dir + "big", out) == BinaryUtil::kVersionV5);
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
    SECTION("legacy v2 files stay readable and are upgraded to v4 on save");
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

    // the files are now v5
    for (const char *name: {"wallets", "state", "cardWallets"}) {
        std::ifstream f(dir + name, std::ios::binary);
        char hb[5] = {};
        f.read(hb, 5);
        CHECK(std::memcmp(hb, "CWCP", 4) == 0);
        CHECK(hb[4] == BinaryUtil::kVersionV5);
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

// The v3 -> v4 format split (the review fix for the mid-record
// excludedFromTotals byte): files the PRE-Curve device builds wrote
// (version byte 3, transaction record with ONE flag byte) must still
// read - with excludedFromTotals defaulting to false - and a legacy
// store is re-saved in v4 on the next save. The legacy bytes are
// written by hand here: the byte-exact mirror of the CURRENT
// writeTransaction MINUS the excludedFromTotals byte is what pins the
// old layout (the truncation section pins the mid-record guard).
static void testSaveFormatV3LegacyRead() {
    SECTION("legacy v3 store (one flag byte) reads, v3 flag defaults off, save upgrades to v4");
    const std::string dir =
            (std::filesystem::temp_directory_path() / "nf_tx_core_tests_v3legacy").string() + "/";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // Little-endian byte assembler for the legacy v3 files.
    auto bytes = std::vector<char>{};
    auto push_u8 = [&bytes](uint8_t v) { bytes.push_back(static_cast<char>(v)); };
    auto push_i32 = [&bytes](int32_t v) {
        const uint32_t u = static_cast<uint32_t>(v);
        for (int i = 0; i < 4; i++) bytes.push_back(static_cast<char>((u >> (8 * i)) & 0xFF));
    };
    auto push_f64 = [&bytes](double v) {
        uint64_t u = 0;
        std::memcpy(&u, &v, sizeof(u));
        for (int i = 0; i < 8; i++) bytes.push_back(static_cast<char>((u >> (8 * i)) & 0xFF));
    };
    auto push_str = [&bytes, &push_i32](const std::string &s) {
        push_i32(static_cast<int32_t>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    };
    auto header = [&bytes, &push_u8](uint8_t version) {
        bytes.insert(bytes.end(), BinaryUtil::kMagic, BinaryUtil::kMagic + 4);
        push_u8(version);
    };
    auto write_file = [&dir, &bytes](const std::string &name) {
        std::ofstream f(dir + name, std::ios::binary | std::ios::trunc);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.clear();   // the next file assembles fresh
    };

    // wallets: ONE wallet (BTC) with ONE transaction in the LEGACY layout
    // (isOutsideTransaction byte only - no excludedFromTotals byte).
    {
        header(BinaryUtil::kVersionV3);
        push_i32(1);   // 1 wallet (list counts are u32)
        // wallet fixed fields
        push_i32(1);                    // walletId
        push_f64(100.0);                // balance
        push_f64(100.0);                // nativeBalance
        push_f64(0.0);                  // bonusBalance
        push_f64(50.0);                 // moneySpent
        push_u8(0);                     // flags (not outside)
        push_str("BTC");                // currencyType
        push_str("");                   // notes
        push_i32(1);                    // 1 transaction
        // transaction, legacy v3 layout
        push_i32(1);                    // transactionId
        push_i32(1);                    // walletId
        push_i32(0);                    // fromWalletId
        push_i32(0);                    // transactionTypeOrdinal
        // date 2026-03-01 09:30:00 (the file stores the ABSOLUTE year;
        // the reader restores tm_year = year - 1900)
        push_i32(2026);
        push_u8(3);                     // month (stored as tm_mon + 1)
        push_u8(1);                     // mday
        push_u8(9);                     // hour
        push_u8(30);                    // min
        push_u8(0);                     // sec
        push_f64(-50.0);                // amount
        push_f64(0.0);                  // toAmount
        push_f64(-50.0);                // nativeAmount
        push_f64(0.0);                  // amountBonus
        push_u8(0);                     // the ONE flag byte (isOutsideTransaction)
        push_str("Legacy Shop");        // description
        push_str("BTC");                // currencyType
        push_str("");                   // toCurrencyType
        push_str("Legacy Shop");        // transactionTypeString
        push_str("");                   // transactionHash
        push_str("");                   // notes
        write_file("wallets");
    }
    // cardWallets: empty store
    {
        header(BinaryUtil::kVersionV3);
        push_i32(0);
        write_file("cardWallets");
    }
    // state: hasTxData | isReadyFlag, counters, one currency, no card types
    {
        header(BinaryUtil::kVersionV3);
        push_u8(1 | 4);   // flags
        push_i32(2);      // txIdCounter
        push_i32(2);      // walletIdCounter
        push_i32(1);      // 1 currency
        push_str("BTC");
        push_i32(0);      // 0 card tx types
        write_file("state");
    }

    // Direct reader: the version byte is honoured, the record reads with
    // the ONE-flag-byte layout.
    {
        std::vector<WalletStruct> direct;
        CHECK(BinaryUtil::readWalletStore(dir + "wallets", direct) == BinaryUtil::kVersionV3);
        CHECK(direct.size() == 1);
        CHECK(direct[0].transactions.size() == 1);
        CHECK(direct[0].transactions[0].description == "Legacy Shop");
        CHECK(direct[0].transactions[0].excludedFromTotals == false);
    }

    // Full pipeline: load the legacy dir...
    TransactionManager tm;
    tm.loadData(dir);
    {
        const auto st = tm.getTransactionManagerState();
        CHECK(st.hasTxData);
        CHECK(!st.hasCardTxData);
        CHECK(st.isReadyFlag);
        CHECK(tm.getWallets().size() == 1);
        CHECK_NEAR(static_cast<double>(tm.getWallets().at("BTC").getBalance()), 100.0, 1e-12);
        CHECK_NEAR(static_cast<double>(tm.getWallets().at("BTC").getMoneySpent()), 50.0, 1e-12);
        const auto txs = tm.getTransactions();
        CHECK(txs.size() == 1);
        CHECK(!txs[0].isExcludedFromTotals());   // the missing v3 byte = false, never garbage
        CHECK(txs[0].getTransactionData().description == "Legacy Shop");
        const auto &d = txs[0].getTransactionData();
        CHECK(d.transactionDate.tm_year == 2026 - 1900);
        CHECK(d.transactionDate.tm_mon == 2);
        CHECK(d.transactionDate.tm_mday == 1);
    }

    // ...re-save -> CURRENT format (v5, incl. the sheet section) on every
    // file...
    CHECK(tm.saveData(dir));
    for (const char *name: {"wallets", "state", "cardWallets"}) {
        std::ifstream f(dir + name, std::ios::binary);
        char hb[5] = {};
        f.read(hb, 5);
        CHECK(std::memcmp(hb, "CWCP", 4) == 0);
        CHECK(hb[4] == BinaryUtil::kVersionV5);
    }

    // ...and the v4 round trip keeps the row (flag still false).
    {
        TransactionManager loaded;
        loaded.loadData(dir);
        CHECK(loaded.getWallets().size() == 1);
        const auto txs = loaded.getTransactions();
        CHECK(txs.size() == 1);
        CHECK(!txs[0].isExcludedFromTotals());
        CHECK_NEAR(static_cast<double>(loaded.getWallets().at("BTC").getBalance()), 100.0, 1e-12);
    }
    SECTION("a wallet store truncated mid-record reads as absent (the loader guard)");
    {
        // [dir] still holds the v4 store of the first section: cut the
        // wallet file mid-record (20 bytes from the end lands inside the
        // first transaction's string region) into a side-by-side dir.
        const std::string cut = dir + "cut/";
        std::filesystem::remove_all(cut);
        std::filesystem::create_directories(cut);
        std::ifstream in(dir + "wallets", std::ios::binary);
        std::istreambuf_iterator<char> inBegin(in), inEnd;
        const std::vector<char> all(inBegin, inEnd);
        const size_t keep = all.size() - 20;
        std::ofstream out(cut + "wallets", std::ios::binary | std::ios::trunc);
        out.write(all.data(), static_cast<std::streamsize>(keep));
        for (const char *name: {"state", "cardWallets"}) {
            std::filesystem::copy(dir + name, cut + name,
                                  std::filesystem::copy_options::overwrite_existing);
        }
        // The reader refuses the half-read store...
        std::vector<WalletStruct> out2;
        CHECK(BinaryUtil::readWalletStore(cut + "wallets", out2) == 0);
        CHECK(out2.empty());
        // ...and the loader treats an inconsistent store as absent.
        TransactionManager tm2;
        tm2.loadData(cut);
        const auto st = tm2.getTransactionManagerState();
        CHECK(!st.hasTxData);
        CHECK(tm2.getWallets().empty());
        CHECK(tm2.getTransactions().empty());
    }
    std::filesystem::remove_all(dir);
}

// The user-owned balance sheet (liabilities): the line serde (the single
// escape rule, upsert/delete, validation), the closed-form amortization
// math (hand-computed fixtures), the v5 storage (round trip, the v3/v4
// upgrade-on-save, clearAll preservation) and the load-bearing state
// semantics on the C surface (the init/restore sequence must not wipe
// the sheet).
static void testBalanceSheet() {
    SECTION("serde: escape round trip, upsert, delete, validation");
    {
        BalanceSheet sheet;
        // The ';;' inside the label is the single escape-rule case (the
        // unescaped label is "Mort;gage").
        const std::string line = "L;uuid-1;Mort;;gage;150000;3.575;2019-02-01;2049-02-01;899.99";
        CHECK(sheet.setLine(line));
        std::tm asof{};
        asof.tm_year = 2026 - 1900;
        asof.tm_mon = 0;
        asof.tm_mday = 15;
        auto lines = sheet.listLines("L", asof);
        CHECK(lines.size() == 1);
        // The unescaped label survives the escape round trip inside the line.
        const std::vector<std::string> f = SheetSerde::splitLine(lines[0]);
        CHECK(f.size() == 10);   // 8 fields + "bal" + the balance value
        CHECK(f[2] == "Mort;gage");
        CHECK(f[8] == "bal");
        // The list form round-trips through set as-is (the trailer is
        // recomputed, so the set is an upsert, not an add).
        CHECK(sheet.setLine(lines[0]));
        CHECK(sheet.getLiabilities().size() == 1);

        // Upsert by id: same id, new label -> replaced in place.
        CHECK(sheet.setLine("L;uuid-1;Renovated;150000;3.575;2019-02-01;-;-") );
        CHECK(sheet.getLiabilities().size() == 1);
        CHECK(sheet.getLiabilities()[0].label == "Renovated");
        CHECK(!sheet.getLiabilities()[0].hasEndDate);
        CHECK(sheet.getLiabilities()[0].monthlyPaymentEur == 0.0);

        // A second, distinct entry keeps its stored position.
        CHECK(sheet.setLine("L;uuid-2;Car loan;20000;4.9;2024-05-01;-;-"));
        CHECK(sheet.getLiabilities().size() == 2);
        CHECK(sheet.getLiabilities()[1].id == "uuid-2");

        // Delete: the second entry goes, the first stays.
        CHECK(sheet.deleteEntry("Luuid-2"));
        CHECK(sheet.getLiabilities().size() == 1);
        CHECK(!sheet.deleteEntry("Luuid-2"));   // already gone
        CHECK(!sheet.deleteEntry("Xuuid-1"));   // unknown kind

        // Validation: every rejection leaves the sheet untouched (size 1).
        CHECK(!sheet.setLine("L;u3;X;100;1;2020-01-01;-;-;"));       // 9 fields
        CHECK(!sheet.setLine("L;u3;X;100;1;2020-01-01;-;"));          // trailing empty field
        CHECK(!sheet.setLine("A;u3;X;100;1;2020-01-01;-;-"));         // kind not active
        CHECK(!sheet.setLine("L;u3;X;abc;1;2020-01-01;-;-"));         // rate: not a number
        CHECK(!sheet.setLine("L;u3;X;100;1;2024-02-30;-;-"));         // no such day
        CHECK(!sheet.setLine("L;u3;X;100;1;2020-13-01;-;-"));         // month 13
        CHECK(!sheet.setLine("L;u3;X;100;1;2020-01-01;2019-01-01;-"));  // end before start
        CHECK(!sheet.setLine("L;u3;X;-100;1;2020-01-01;-;-"));        // negative principal
        CHECK(!sheet.setLine("L;u3;X;100;1;2020-01-01;-;1.5e"));      // broken decimal
        CHECK(!sheet.setLine("L;;X;100;1;2020-01-01;-;-"));           // empty id
        CHECK(!sheet.setLine("L;u3;X;100;1;2020-01-01;-;nan"));       // not finite
        CHECK(sheet.getLiabilities().size() == 1);
        CHECK(sheet.getLiabilities()[0].id == "uuid-1");

        // serialize/parse round trip (the v5 section text).
        BalanceSheet other;
        const std::string text = sheet.serialize(asof);
        CHECK(other.parse(text) == 0);
        CHECK(other.getLiabilities().size() == 1);
        CHECK(other.getLiabilities()[0].label == "Renovated");
        CHECK(other.serialize(asof) == text);
    }

    SECTION("balance math: hand-computed amortization fixtures");
    {
        auto tmOf = [](int year, int month, int day) {
            std::tm t{};
            t.tm_year = year - 1900;
            t.tm_mon = month - 1;
            t.tm_mday = day;
            return t;
        };
        // A: standard annuity - P=100000, 4.8%/year (i=0.004), M=600,
        // 36 whole months: 100000*1.004^36 - 600*(1.004^36-1)/0.004.
        Liability a;
        a.principalEur = 100000.0;
        a.annualRatePct = 4.8;
        a.startYear = 2020; a.startMonth = 1; a.startDay = 1;
        a.monthlyPaymentEur = 600.0;
        CHECK_NEAR(a.balanceAsOf(tmOf(2023, 1, 1)), 92272.378308, 1e-3);
        // B: zero rate - P=12000, M=500, 8 months: 12000 - 8*500.
        Liability b;
        b.principalEur = 12000.0;
        b.startYear = 2026; b.startMonth = 1; b.startDay = 1;
        b.monthlyPaymentEur = 500.0;
        CHECK_NEAR(b.balanceAsOf(tmOf(2026, 9, 15)), 8000.0, 1e-9);
        // C: the payoff cap - an overpay settles at 0, never negative.
        Liability c;
        c.principalEur = 1000.0;
        c.startYear = 2026; c.startMonth = 1; c.startDay = 1;
        c.monthlyPaymentEur = 10000.0;
        CHECK_NEAR(c.balanceAsOf(tmOf(2026, 2, 1)), 0.0, 1e-9);
        // D: the end date - 1000, 12%/year (i=0.01), M=80, ending after
        // 11 months: full annuity value AT the end month, 0 past it.
        Liability d;
        d.principalEur = 1000.0;
        d.annualRatePct = 12.0;
        d.startYear = 2020; d.startMonth = 1; d.startDay = 1;
        d.hasEndDate = true;
        d.endYear = 2020; d.endMonth = 12; d.endDay = 1;
        d.monthlyPaymentEur = 80.0;
        CHECK_NEAR(d.balanceAsOf(tmOf(2020, 12, 1)), 190.321573, 1e-3);
        CHECK_NEAR(d.balanceAsOf(tmOf(2021, 1, 1)), 0.0, 1e-9);
        // E: no payment, rate > 0 - pure compounding P(1+i)^n:
        // 100 * 1.01^10.
        Liability e;
        e.principalEur = 100.0;
        e.annualRatePct = 12.0;
        e.startYear = 2020; e.startMonth = 1; e.startDay = 1;
        CHECK_NEAR(e.balanceAsOf(tmOf(2020, 11, 1)), 110.462213, 1e-3);
        // E2: no payment, zero rate - the principal stands.
        Liability e2;
        e2.principalEur = 555.0;
        e2.startYear = 2020; e2.startMonth = 1; e2.startDay = 1;
        CHECK_NEAR(e2.balanceAsOf(tmOf(2020, 6, 1)), 555.0, 1e-9);
        // F: a future start - 0 elapsed months, the full principal.
        Liability f;
        f.principalEur = 321.0;
        f.annualRatePct = 5.0;
        f.startYear = 2030; f.startMonth = 1; f.startDay = 1;
        f.monthlyPaymentEur = 10.0;
        CHECK_NEAR(f.balanceAsOf(tmOf(2026, 1, 1)), 321.0, 1e-9);
    }

    SECTION("assets: serde round trip, sum, category validation");
    {
        BalanceSheet sheet;
        // A ';' in the label + a ';' in the note - the single escape rule.
        const std::string artLine =
                "A;a-1;Art ;;carrying case;ART;12000;2026-11-01T09:30:00;The ;;frame;; is original";
        const std::string houseLine = "A;a-2;House;REAL_ESTATE;350000;2025-06-15T14:05:09;-";
        CHECK(sheet.setLine(artLine));
        CHECK(sheet.setLine(houseLine));
        CHECK(sheet.getAssets().size() == 2);
        CHECK(sheet.getAssets()[0].label == "Art ;carrying case");
        CHECK(sheet.getAssets()[0].note == "The ;frame; is original");
        CHECK(sheet.assetsTotal() == 362000.0);

        // listLines("A") = the stored lines (no trailer - nothing is
        // core-computed for a static value).
        std::tm asof{};
        asof.tm_year = 2026 - 1900;
        asof.tm_mon = 0;
        asof.tm_mday = 15;
        const std::vector<std::string> lines = sheet.listLines("A", asof);
        CHECK(lines.size() == 2);
        CHECK(lines[0] == artLine);
        CHECK(lines[1] == houseLine);
        // Re-setting a listed line = an idempotent upsert.
        CHECK(sheet.setLine(lines[1]));
        CHECK(sheet.getAssets().size() == 2);

        // Upsert in place: the id keeps its position, the value (and the
        // sum) moves, the user stamp carries through.
        CHECK(sheet.setLine("A;a-2;House (sale pending);REAL_ESTATE;375000;2026-11-17T18:00:00;-"));
        CHECK(sheet.getAssets().size() == 2);
        CHECK(sheet.getAssets()[1].id == "a-2");
        CHECK(sheet.getAssets()[1].valueEur == 375000.0);
        CHECK(sheet.getAssets()[1].updated == "2026-11-17T18:00:00");
        CHECK(sheet.assetsTotal() == 387000.0);

        // Every one of the six tokens parses to its enum and emits back
        // the same token (the core is the vocabulary owner).
        CHECK(sheet.setLine("A;a-3;Bike;VEHICLE;5000;2020-01-01T12:00:00;-"));
        CHECK(sheet.setLine("A;a-4;Stamps;COLLECTIBLE;100;2020-01-01T12:00:00;-"));
        CHECK(sheet.setLine("A;a-5;Gold bars;COMMODITY;10;2020-01-01T12:00:00;-"));
        CHECK(sheet.setLine("A;a-6;Misc;OTHER;0;2020-01-01T12:00:00;-"));
        CHECK(sheet.getAssets().size() == 6);
        const char *tokens[] = {"ART", "REAL_ESTATE", "VEHICLE", "COLLECTIBLE", "COMMODITY", "OTHER"};
        for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
            CHECK(std::string(categoryToken(sheet.getAssets()[i].category)) == tokens[i]);
        }
        CHECK(sheet.assetsTotal() == 392110.0);

        // Validation: bad categories and shapes are rejected, the sheet
        // stays untouched (the serde IS the validator).
        CHECK(!sheet.setLine("A;a-9;X;GOLD;100;2026-01-01T00:00:00;-"));         // unknown category
        CHECK(!sheet.setLine("A;a-9;X;real_estate;100;2026-01-01T00:00:00;-"));   // case-sensitive
        CHECK(!sheet.setLine("A;a-9;X;;100;2026-01-01T00:00:00;-"));              // empty category
        CHECK(!sheet.setLine("A;a-9;X;ART;100;2026-01-01T00:00:00;-;"));          // 8 fields
        CHECK(!sheet.setLine("A;a-9;X;ART;-50;2026-01-01T00:00:00;-"));           // negative value
        CHECK(!sheet.setLine("A;a-9;X;ART;100;2026-02-30T00:00:00;-"));           // no such day
        CHECK(!sheet.setLine("A;a-9;X;ART;100;2026-01-01T25:00:00;-"));           // hour 25
        CHECK(!sheet.setLine("A;a-9;X;ART;100;2026-01-01T00:00;-"));              // short timestamp
        CHECK(sheet.getAssets().size() == 6);

        // Mixed-collection section round trip: L and A lines in one text,
        // dispatched by kind, byte-stable (the section is generic).
        BalanceSheet mixed;
        CHECK(mixed.setLine("L;l-1;Loan;1000;2;2020-01-01;2030-01-01;25"));
        CHECK(mixed.setLine(artLine));
        const std::string section = mixed.serialize(asof);
        BalanceSheet back;
        CHECK(back.parse(section) == 0);
        CHECK(back.getLiabilities().size() == 1);
        CHECK(back.getAssets().size() == 1);
        CHECK(back.assetsTotal() == 12000.0);
        CHECK(back.serialize(asof) == section);

        // Delete is kind-scoped (the app passes "A" + the id).
        CHECK(!sheet.deleteEntry("La-1"));   // the L collection: no such entry
        CHECK(sheet.deleteEntry("Aa-1"));
        CHECK(sheet.getAssets().size() == 5);
        CHECK(!sheet.deleteEntry("Aa-1"));   // already gone
        CHECK(sheet.assetsTotal() == 380110.0);
    }

    SECTION("v5 storage: sheet round trip, v3/v4 upgrade, clearAll preservation");
    const std::string dir =
            (std::filesystem::temp_directory_path() / "nf_tx_core_tests_sheet").string() + "/";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // v5 round trip with content: the section text rides the state file
    // byte-exact, clearAll (the import-state reset) does not touch it.
    {
        BalanceSheet sheet;
        CHECK(sheet.setLine("L;a1;Mort;;gage;150000;3.575;2019-02-01;2049-02-01;899.99"));
        CHECK(sheet.setLine("L;a2;Car;20000;4.9;2024-05-01;-;-"));
        // The section is generic over the collections: an asset line
        // rides the same text byte-exact through the save file.
        CHECK(sheet.setLine("A;as-1;House;REAL_ESTATE;350000;2026-11-15T10:00:00;-"));
        CHECK(sheet.setLine("G;g-1;First house;500000;2032-06-30;ASSETS"));
        std::tm asof{};
        asof.tm_year = 2026 - 1900;
        asof.tm_mon = 0;
        asof.tm_mday = 15;
        const std::string text = sheet.serialize(asof);

        TransactionManager tm;
        tm.setBalanceSheetText(text);
        CHECK(tm.saveData(dir));
        for (const char *name: {"wallets", "state", "cardWallets"}) {
            std::ifstream f(dir + name, std::ios::binary);
            char hb[5] = {};
            f.read(hb, 5);
            CHECK(std::memcmp(hb, "CWCP", 4) == 0);
            CHECK(hb[4] == BinaryUtil::kVersionV5);
        }
        // The section is stored as plain text inside the file (the label
        // escaped per the line rule).
        {
            std::ifstream f(dir + "state", std::ios::binary);
            std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(raw.find("Mort;;gage") != std::string::npos);
        }

        TransactionManager tm2;
        tm2.loadData(dir);
        CHECK(tm2.getBalanceSheetText() == text);
        // The clearAll preservation IN the transport: the reset wipes the
        // (empty) import state, the sheet section survives it.
        tm2.clearAll();
        CHECK(tm2.getWallets().empty());
        CHECK(tm2.getBalanceSheetText() == text);
        CHECK(tm2.saveData(dir));

        BalanceSheet loaded;
        CHECK(loaded.parse(tm2.getBalanceSheetText()) == 0);
        CHECK(loaded.getLiabilities().size() == 2);
        CHECK(loaded.getLiabilities()[0].label == "Mort;gage");
        CHECK(loaded.getAssets().size() == 1);
        CHECK(loaded.getAssets()[0].valueEur == 350000.0);
        CHECK(loaded.getGoals().size() == 1);
        CHECK(loaded.getGoals()[0].label == "First house");
        CHECK(loaded.getGoals()[0].targetEur == 500000.0);
        CHECK(loaded.serialize(asof) == text);
    }

    // Little-endian byte assembler for the LEGACY v3/v4 stores (their
    // state layout is the current one MINUS the v5 sheet section at the
    // tail; the wallet stores are empty).
    auto assembleLegacyState = [&](uint8_t version) {
        const std::string sub = dir + "legacy" + std::to_string(version) + "/";
        std::filesystem::remove_all(sub);
        std::filesystem::create_directories(sub);
        auto writeHeader = [&version](std::ofstream &f) {
            f.write(BinaryUtil::kMagic, 4);
            f.write(reinterpret_cast<const char *>(&version), 1);
        };
        const int32_t zero = 0;
        {
            std::ofstream f(sub + "wallets", std::ios::binary | std::ios::trunc);
            writeHeader(f);
            f.write(reinterpret_cast<const char *>(&zero), 4);   // 0 wallets
        }
        {
            std::ofstream f(sub + "cardWallets", std::ios::binary | std::ios::trunc);
            writeHeader(f);
            f.write(reinterpret_cast<const char *>(&zero), 4);   // 0 wallets
        }
        {
            std::ofstream f(sub + "state", std::ios::binary | std::ios::trunc);
            writeHeader(f);
            auto push_u8 = [&f](uint8_t v) { f.write(reinterpret_cast<const char *>(&v), 1); };
            auto push_i32 = [&f](int32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
            auto push_str = [&f, &push_i32](const std::string &s) {
                const int32_t n = static_cast<int32_t>(s.size());
                f.write(reinterpret_cast<const char *>(&n), 4);
                f.write(s.data(), static_cast<std::streamsize>(n));
            };
            // hasTxData, counters, one currency, no card types, NO section
            push_u8(1);
            push_i32(9);
            push_i32(9);
            push_i32(1);
            push_str("BTC");
            push_i32(0);
        }
        return sub;
    };

    // v3 -> v5 and v4 -> v5: the legacy state reads with the EMPTY sheet
    // (the in-memory upgrade) and re-saves as v5 with the section intact.
    for (const uint8_t legacy: {(uint8_t) BinaryUtil::kVersionV3, (uint8_t) BinaryUtil::kVersionV4}) {
        const std::string legacyDir = assembleLegacyState(legacy);
        TransactionManager tm;
        tm.loadData(legacyDir);
        CHECK(tm.getWallets().empty());       // the empty store read clean
        CHECK(tm.getBalanceSheetText().empty());   // pre-v5: no section
        CHECK(tm.getSaveDir() == legacyDir);   // the loaded dir is the auto-save target
        tm.setBalanceSheetText("L;z1;Legacy sheet;100;0;2020-01-01;-;-");
        CHECK(tm.saveData(legacyDir));
        std::ifstream f(legacyDir + "state", std::ios::binary);
        char hb[5] = {};
        f.read(hb, 5);
        CHECK(hb[4] == BinaryUtil::kVersionV5);
        TransactionManager tm2;
        tm2.loadData(legacyDir);
        CHECK(tm2.getBalanceSheetText() == "L;z1;Legacy sheet;100;0;2020-01-01;-;-");
    }

    SECTION("user sheet: the init/restore sequence preserves it (C surface)");
    {
        const std::string saveDir =
                (std::filesystem::temp_directory_path() / "nf_tx_core_tests_sheet_app")
                        .string() + "/";
        std::filesystem::remove_all(saveDir);
        std::filesystem::create_directories(saveDir);

        DataHolder &dh = DataHolder::GetInstance();

        // A restore-style cold start: a fresh manager, init finds no store.
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
        CHECK(!init("tests.log", std::string()));
        CHECK(!dh.isInitialized());

        // The user adds a liability. The core auto-save is skipped: no
        // store location is known yet (nothing loaded or saved on this
        // core incarnation) - the entry is in memory, the app's regular
        // save (after the next import) persists it.
        const std::string entry = "L;loan-1;Mortgage;150000;3.575;2019-02-01;2049-02-01;899.99";
        CHECK(sheetSet(entry));
        CHECK(sheetList("L").size() == 1);
        CHECK(sheetList("L")[0].find(";Mortgage;") != std::string::npos);
        CHECK(sheetList("L")[0].find(";bal;") != std::string::npos);

        // The Firebase-restore shape: a NEW manager (the init of the next
        // incarnation) + clearAll + the set*Data sequence. The sheet lives
        // in the holder, so every step of it leaves the entry in place.
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
        clearAll();
        CHECK(getActiveModes() == 0);
        setWalletData({walletDataXml(1, "BTC", 2.5)});
        setTransactionData({txDataXml(10, "BTC", 2.5)});
        CHECK(getActiveModes() == 1);
        CHECK(sheetList("L").size() == 1);   // survived the whole sequence

        // The goal collection rides the SAME C surface (no new names):
        // set, list, the GP line in the snapshot, delete. The set+delete
        // pair nets to nothing in the auto-saved store behind us.
        CHECK(sheetSet("G;cg-1;Demo;1000;-;ASSETS"));
        CHECK(sheetList("G").size() == 1);
        {
            const std::vector<std::string> gSnap = sheetSnapshot();
            CHECK(gSnap.size() == 2);
            // Cold core: no tracked value at all, zero progress, no
            // crossing (0 < 1).
            CHECK(gSnap[1] == "GP;cg-1;0;1000;0;-");
        }
        CHECK(sheetDelete("Gcg-1"));
        CHECK(sheetList("G").empty());

        // The app's post-import save lands the v5 store: the import state
        // AND the sheet section (the holder injects it right before the
        // write, so the restore sequence above could not lose it).
        CHECK(save(saveDir));
        {
            std::ifstream f(saveDir + "state", std::ios::binary);
            char hb[5] = {};
            f.read(hb, 5);
            CHECK(hb[4] == BinaryUtil::kVersionV5);
            std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(raw.find("Mortgage") != std::string::npos);
        }

        // Next cold start: a fresh manager loads the store (leg 1) - the
        // import state resets to the saved rows, the sheet comes back
        // from the v5 section.
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
        dh.loadData(saveDir);
        CHECK(getActiveModes() == 1);   // the restored import state
        auto lines = sheetList("L");
        CHECK(lines.size() == 1);
        CHECK(lines[0].find(";Mortgage;") != std::string::npos);
        CHECK(dh.getSheet().getLiabilities()[0].principalEur == 150000.0);

        // Edit the entry (upsert) - the core auto-saves to the loaded dir.
        CHECK(sheetSet("L;loan-1;Mortgage (renovated);160000;3.575;2019-02-01;2049-02-01;950.00"));
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
        dh.loadData(saveDir);
        CHECK(sheetList("L")[0].find(";Mortgage (renovated);") != std::string::npos);

        // Delete the entry - the section comes back empty on reload.
        CHECK(sheetDelete("Lloan-1"));
        CHECK(sheetList("L").empty());
        CHECK(!sheetDelete("Lloan-1"));   // already gone
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
        dh.loadData(saveDir);
        CHECK(sheetList("L").empty());
        CHECK(dh.getSheet().getLiabilities().empty());

        // Cold DataHolder again: the next test (testImportStateSemantics)
        // starts from a never-ready manager.
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
        CHECK(!dh.isInitialized());

        // The snapshot on that cold core: empty sheet, no priced wallets
        // - exactly the zero line (the UI hides it; never an error).
        CHECK(sheetSnapshot().front() == "NW;0;0;0;0");
    }

    SECTION("snapshot: the net-worth contract (the NW line)");
    {
        std::tm asof{};
        asof.tm_year = 2026 - 1900;
        asof.tm_mon = 0;
        asof.tm_mday = 15;

        // 1. Empty sheet, no tracked value: the zero snapshot (EXACTLY
        // the line the screen shows before any entry exists).
        BalanceSheet empty;
        const std::vector<std::string> zero = empty.snapshot(0.0, asof);
        CHECK(zero.size() == 1);
        CHECK(zero[0] == "NW;0;0;0;0");

        // 2. Hand-computable: one annuity at n=12 (P=100000, 4.8% p.a.
        // -> i=0.004, M=800: 100000*1.004^12 - 800*(1.004^12-1)/0.004
        // = 95092.97925) + a zero-rate one at n=6 (12000 - 6*500 = 9000),
        // two assets (355000), the tracked value injected by the caller.
        BalanceSheet sheet;
        CHECK(sheet.setLine("L;n-1;Mortgage;100000;4.8;2025-01-01;-;800"));
        CHECK(sheet.setLine("L;n-2;Car;12000;0;2025-07-01;-;500"));
        CHECK(sheet.setLine("A;n-3;House;REAL_ESTATE;350000;2025-06-15T14:05:09;-"));
        CHECK(sheet.setLine("A;n-4;Bike;VEHICLE;5000;2020-01-01T12:00:00;-"));
        const std::vector<std::string> snap = sheet.snapshot(1234.56, asof);
        CHECK(snap.size() == 1);
        CHECK(snap[0] == "NW;1234.56;355000;104092.9792;252141.5808");

        // 3. Net worth SIGNED: debts above assets stay negative (never
        // clamped like the non-negative line fields). A future start
        // keeps the balance the full principal for this asOf, so the
        // figure is determinate on the wall clock.
        BalanceSheet underwater;
        CHECK(underwater.setLine("L;z-1;Future;2500;0;2035-01-01;-;-"));
        CHECK(underwater.setLine("A;z-2;Books;ART;500;2020-01-01T12:00:00;-"));
        const std::vector<std::string> neg = underwater.snapshot(0.0, asof);
        CHECK(neg.size() == 1);
        CHECK(neg[0] == "NW;0;500;2500;-2000");

        // 4. A NEGATIVE tracked value (the internal EUR buffer can run
        // below zero - fiat spent above the tracked deposits - and so
        // can a wallet): the field stays signed end-to-end, and the net
        // sees the true value, not the clamped one.
        BalanceSheet negTracked;
        CHECK(negTracked.setLine("L;t-1;Future;2500;0;2035-01-01;-;-"));
        const std::vector<std::string> negT = negTracked.snapshot(-44.05, asof);
        CHECK(negT.size() == 1);
        CHECK(negT[0] == "NW;-44.05;0;2500;-2544.05");
    }

    SECTION("goals: serde (the G line) + metric vocabulary");
    {
        BalanceSheet sheet;
        std::tm asof{};
        asof.tm_year = 2026 - 1900;
        asof.tm_mon = 0;
        asof.tm_mday = 15;
        // The full valid shape: with/without date, an escaped label, and
        // the degenerate target 0 (legal, the marker comes from the GP).
        CHECK(sheet.setLine("G;g1;Bike;1000;-;ASSETS"));
        CHECK(sheet.setLine("G;g2;First house;350000;2030-12-31;ASSETS"));
        CHECK(sheet.setLine("G;g3;Mort;;gage free;1234560;2035-01-01;NET_WORTH"));
        CHECK(sheet.setLine("G;g4;Open goal;0;-;NET_WORTH"));
        auto lines = sheet.listLines("G", asof);
        CHECK(lines.size() == 4);
        CHECK(lines[0] == "G;g1;Bike;1000;-;ASSETS");
        CHECK(lines[1] == "G;g2;First house;350000;2030-12-31;ASSETS");
        CHECK(lines[2] == "G;g3;Mort;;gage free;1234560;2035-01-01;NET_WORTH");
        CHECK(lines[3] == "G;g4;Open goal;0;-;NET_WORTH");
        CHECK(sheet.getGoals()[2].label == "Mort;gage free");
        CHECK(sheet.getGoals()[1].hasTargetDate);
        CHECK(sheet.getGoals()[1].targetDate == "2030-12-31");
        CHECK(!sheet.getGoals()[0].hasTargetDate);

        // Upsert keeps the position; delete is kind-scoped; a second
        // delete on the same id is false.
        CHECK(sheet.setLine("G;g1;New bike;2000;2031-06-30;ASSETS"));
        CHECK(sheet.listLines("G", asof).size() == 4);
        CHECK(sheet.listLines("G", asof)[0] == "G;g1;New bike;2000;2031-06-30;ASSETS");
        CHECK(sheet.deleteEntry("Gg3"));
        CHECK(sheet.listLines("G", asof).size() == 3);
        CHECK(!sheet.deleteEntry("Gg3"));

        // The serde IS the validator: a rejected line leaves the sheet
        // untouched (bad metric, negative target, impossible date, bad
        // number, wrong field count).
        const size_t before = sheet.getGoals().size();
        CHECK(!sheet.setLine("G;g9;Bad metric;1000;-;CRYPTO"));
        CHECK(!sheet.setLine("G;g9;Negative target;-5;-;ASSETS"));
        CHECK(!sheet.setLine("G;g9;Bad date;1000;2026-1-01;ASSETS"));
        CHECK(!sheet.setLine("G;g9;Impossible day;1000;2026-02-30;ASSETS"));
        CHECK(!sheet.setLine("G;g9;Not a number;many;2035-01-01;ASSETS"));
        CHECK(!sheet.setLine("G;g9;Too few;1000;ASSETS"));
        CHECK(sheet.getGoals().size() == before);

        // parse dispatch: a bad G line is rejected + counted, the good
        // lines (all three kinds) still load.
        BalanceSheet mixed;
        const size_t rejected = mixed.parse(
                "G;m1;Good;1000;-;ASSETS\n"
                "G;m2;Bad metric;1000;-;NOPE\n"
                "A;m3;Car;VEHICLE;100;2026-01-01T00:00:00;-\n"
                "L;m4;Loan;100;0;2020-01-01;-;10");
        CHECK(rejected == 1);
        CHECK(mixed.getGoals().size() == 1);
        CHECK(mixed.getAssets().size() == 1);
        CHECK(mixed.getLiabilities().size() == 1);
    }

    SECTION("goals: the GP progress lines (hand fixtures)");
    {
        std::tm asof{};
        asof.tm_year = 2026 - 1900;
        asof.tm_mon = 0;
        asof.tm_mday = 15;

        // The same sheet shape as the NW fixture: one hand-computed
        // annuity (n = 12: 100000*1.004^12 - 800*(1.004^12-1)/0.004 =
        // 95092.97925), the injected tracked value 1234.56, so net =
        // 1234.56 - 95092.97925 = -93858.41925 (SIGNED).
        BalanceSheet sheet;
        CHECK(sheet.setLine("L;gl-1;Mortgage;100000;4.8;2025-01-01;-;800"));
        CHECK(sheet.setLine("G;p1;Bike;1000;-;ASSETS"));
        CHECK(sheet.setLine("G;p2;Car fund;2500;-;ASSETS"));
        CHECK(sheet.setLine("G;p3;Solvency;100000;-;NET_WORTH"));
        CHECK(sheet.setLine("G;p4;Open goal;0;-;ASSETS"));

        const std::vector<std::string> snap = sheet.snapshot(1234.56, asof);
        CHECK(snap.size() == 5);   // the NW line + one GP line per goal
        CHECK(snap[0] == "NW;1234.56;0;95092.97925;-93858.41925");
        // 1. REACHED: 1234.56 / 1000 = 1.23456 >= 1.
        CHECK(snap[1] == "GP;p1;1234.56;1000;1.23456;reached");
        // 2. UNREACHED: 1234.56 / 2500 = 0.493824 (0 <= p < 1).
        CHECK(snap[2] == "GP;p2;1234.56;2500;0.493824;-");
        // 3. THE METRIC SELECTION on the same sheet: NET_WORTH reads the
        //    net field (SIGNED), ASSETS read the tracked field above -
        //    -93858.41925 / 100000 = -0.9385841925, no crossing.
        CHECK(snap[3] == "GP;p3;-93858.41925;100000;-0.9385841925;-");
        // 4. THE DEGENERATE target (0): the insufficient marker, the core
        //    never divides, no crossing either.
        CHECK(snap[4] == "GP;p4;1234.56;0;-;-");

        // The GP lines come AFTER the NW line, in stored goal order -
        // dispatch is on the kind prefix, never the index.
        CHECK(snap[1].rfind("GP;", 0) == 0);
        CHECK(snap[4].rfind("GP;p4;", 0) == 0);
    }
    std::filesystem::remove_all(dir);
}

// The net-worth snapshot on the PRICED core state: the buffer-inclusion
// semantic (the internal EUR cash buffer counts toward the tracked
// value, unlike every card/donut aggregate) is asserted BY CONSTRUCTION
// - the same priced state the card totals show, plus the buffer.
static void testNetWorthSnapshot() {
    SECTION("snapshot on the priced core state: buffer included (by construction)");
    {
        // 1000 EUR in, 600 EUR spent on 0.006 BTC: the inner EUR cash
        // buffer sits at 400 next to the crypto position.
        std::vector<std::string> data{
                blockPitHeader(),
                bpLine("01.01.2026 21:30:23", "Bitpanda", "Non-Taxable In", "", "", "EUR", "1000"),
                bpLine("02.01.2026 21:30:23", "Bitpanda", "Trade", "EUR", "600", "BTC", "0.006")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK_NEAR(tm->getWallets().at("EUR").getBalance(), 400.0, 1e-9);
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), 0.006, 1e-12);

        const auto &curs = tm->getCurrencies();
        std::vector<double> prices(curs.size(), 0.0);
        for (size_t i = 0; i < curs.size(); i++) {
            if (curs[i] == "BTC") prices[i] = 5000.0;   // 0.006 BTC = 30 EUR
            // EUR MUST be injected explicitly: a cached 0.0 is a "no
            // price" answer that beats the static fiat 1.0 fallback,
            // and would zero the buffer out of the value.
            if (curs[i] == "EUR") prices[i] = 1.0;
        }
        tm->setPrices(prices);
        tm->calculateWalletBalances();

        // The card total (EUR excluded, the D4 scope) and the tracked
        // value the snapshot injects (the buffer IN).
        CHECK_NEAR(tm->getTotalValueOfAssets(), 30.0, 1e-9);
        CHECK_NEAR(tm->getTotalValueOfAssetsIncludingBuffer(), 430.0, 1e-9);

        // The anchoring identity: the last "value" point of the daily
        // series IS the card total (telescoping by construction) - the
        // snapshot's tracked value is that figure plus the buffer.
        double lastValue = 0.0;
        for (const auto &row: tm->getDailySeries()) {
            if (row.rfind("value;", 0) == 0) {
                lastValue = std::stod(row.substr(row.rfind(';') + 1));
            }
        }
        CHECK_NEAR(lastValue, tm->getTotalValueOfAssets(), 1e-6);

        BalanceSheet sheet;
        CHECK(sheet.setLine("A;h-1;House;REAL_ESTATE;200000;2026-01-01T12:00:00;-"));
        const std::vector<std::string> snap = sheet.snapshot(
                tm->getTotalValueOfAssetsIncludingBuffer(), SheetSerde::todayWallClock());
        CHECK(snap.size() == 1);
        CHECK(snap[0] == "NW;430;200000;0;200430");

        // The same state WITHOUT the buffer in the injection: the old
        // card-scope figure (what a D4-scoped display would show).
        const std::vector<std::string> cardScope = sheet.snapshot(
                tm->getTotalValueOfAssets(), SheetSerde::todayWallClock());
        CHECK(cardScope[0] == "NW;30;200000;0;200030");
    }
}

// ---------------------------------------------------------- alert tests ---
//
// The edge-triggered alert state machine (the latched crossing
// automaton, core-owned): fixtures pin each semantic - the crossing up
// and down, the latch (no re-fire), the re-arm by price swing, the
// first-true-fires-once rule, the user-edit re-arm, the skipped symbol
// no longer held, the goal swing (auto re-arm), the degenerate target,
// the serde round trip, and the C surface with the auto-save latch
// surviving an incarnation.
//
// The test lives at the END of main on purpose: it drives the
// DataHolder singleton (the C surface) and leaves it in alert state -
// no later test may depend on a particular sheet state.
static void testAlerts() {
    SECTION("alerts: serde (the AL line) + the GA latch line");
    {
        BalanceSheet sheet;
        std::tm asof{};
        asof.tm_year = 2026 - 1900;
        asof.tm_mon = 2;
        asof.tm_mday = 1;

        // The full valid shape: unfired + fired (the stamp is part of
        // the line), upsert keeps the position, delete is kind-scoped.
        CHECK(sheet.setLine("AL;a1;BTC;ABOVE;60000;0;-"));
        CHECK(sheet.setLine("AL;a2;ETH;BELOW;3000;1;2026-03-01T10:00:00"));
        auto lines = sheet.listLines("AL", asof);
        CHECK(lines.size() == 2);
        CHECK(lines[0] == "AL;a1;BTC;ABOVE;60000;0;-");
        CHECK(lines[1] == "AL;a2;ETH;BELOW;3000;1;2026-03-01T10:00:00");
        CHECK(sheet.setLine("AL;a1;BTC;ABOVE;55000;0;-"));   // upsert = the user edit (re-arm)
        CHECK(sheet.listLines("AL", asof)[0] == "AL;a1;BTC;ABOVE;55000;0;-");
        CHECK(sheet.deleteEntry("ALa2"));
        CHECK(sheet.listLines("AL", asof).size() == 1);
        CHECK(!sheet.deleteEntry("ALa2"));

        // The serde IS the validator (a rejected line leaves the sheet
        // untouched): bad direction, bad symbol, negative / non-numeric
        // threshold, field underflow, bad fired token, and the latch
        // invariant BOTH ways (fired needs its stamp, unfired needs "-").
        const size_t before = sheet.getPriceAlerts().size();
        CHECK(!sheet.setLine("AL;a9;BTC;SIDEWAYS;1;0;-"));
        CHECK(!sheet.setLine("AL;a9;B TC;ABOVE;1;0;-"));
        CHECK(!sheet.setLine("AL;a9;;ABOVE;1;0;-"));
        CHECK(!sheet.setLine("AL;a9;BTC;ABOVE;-1;0;-"));
        CHECK(!sheet.setLine("AL;a9;BTC;ABOVE;many;0;-"));
        CHECK(!sheet.setLine("AL;a9;BTC;ABOVE;1;0"));
        CHECK(!sheet.setLine("AL;a9;BTC;ABOVE;1;2;-"));
        CHECK(!sheet.setLine("AL;a9;BTC;ABOVE;1;1;-"));
        CHECK(!sheet.setLine("AL;a9;BTC;ABOVE;1;0;not-a-stamp"));
        CHECK(sheet.getPriceAlerts().size() == before);

        // The GA line is core-owned state: setLine rejects it (the user
        // cannot edit latches directly), serialize/parse round-trip it.
        CHECK(!sheet.setLine("GA;g1;1;2026-03-01T10:00:00"));
        CHECK(sheet.setLine("G;g1;Fund;100;-;ASSETS"));
        CHECK(sheet.setLine("AL;a3;BTC;ABOVE;1;0;-"));
        {
            std::map<std::string, double> held{{"BTC", 150.0}};
            auto current = [&](const std::string &s, double &p) {
                auto it = held.find(s);
                if (it == held.end()) return false;
                p = it->second;
                return true;
            };
            bool changed = false;
            const auto fired = sheet.alertCheck(150.0, current, asof, changed);
            CHECK(fired.size() == 2);   // the alert (150 > 1) + the goal (150 >= 100)
            CHECK(fired[0] == "FIRED;PRICE;a3;BTC;ABOVE;1;150");
            CHECK(fired[1] == "FIRED;GOAL;g1;Fund;ASSETS;150;100");
            CHECK(changed);
        }
        CHECK(sheet.listLines("AL", asof)[1] ==
              "AL;a3;BTC;ABOVE;1;1;2026-03-01T00:00:00");
        {
            // The fired state persists as lines: the AL latch in place,
            // the goal latch as the GA line (the asOf of the firing pass).
            const std::string text = sheet.serialize(asof);
            CHECK(text.find("GA;g1;1;2026-03-01T00:00:00") != std::string::npos);
            BalanceSheet back;
            CHECK(back.parse(text) == 0);
            CHECK(back.getPriceAlerts().size() == 2);
            CHECK(back.getPriceAlerts()[1].fired);
            CHECK(back.getPriceAlerts()[1].firedIso == "2026-03-01T00:00:00");
            CHECK(back.getGoalLatches().size() == 1);
            CHECK(back.getGoalLatches().at("g1").firedIso == "2026-03-01T00:00:00");
        }
        // The disarm drops the GA line (only the FIRED state is
        // represented; absence = disarmed).
        {
            auto noPrice = [](const std::string &, double &) { return false; };
            bool changed = false;
            const auto fired = sheet.alertCheck(50.0, noPrice, asof, changed);
            CHECK(fired.empty());
            CHECK(changed);   // the goal disarm (50 < 100)
            CHECK(sheet.getGoalLatches().empty());
            CHECK(sheet.serialize(asof).find("GA;") == std::string::npos);
        }
        // An orphan GA line (a hand-edited store) is pruned by the next
        // check and counts as a state change.
        {
            BalanceSheet orphan;
            CHECK(orphan.parse("GA;ghost;1;2026-03-01T00:00:00") == 0);
            CHECK(orphan.getGoalLatches().count("ghost") == 1);
            auto noPrice = [](const std::string &, double &) { return false; };
            bool changed = false;
            const auto fired = orphan.alertCheck(0.0, noPrice, asof, changed);
            CHECK(fired.empty());
            CHECK(changed);
            CHECK(orphan.getGoalLatches().empty());
        }
    }

    SECTION("alerts: the latched crossing automaton (hand fixtures)");
    {
        std::tm asof{};
        asof.tm_year = 2026 - 1900;
        asof.tm_mon = 2;
        asof.tm_mday = 2;
        asof.tm_hour = 10;
        asof.tm_min = 30;
        asof.tm_sec = 0;
        const std::string iso = "2026-03-02T10:30:00";

        // 1. THE CROSSING UP + the latch: no fire below the threshold,
        //    one fire on the crossing, silence afterwards (the latch).
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("AL;p1;BTC;ABOVE;100;0;-"));
            std::map<std::string, double> prices{{"BTC", 90.0}};
            auto current = [&](const std::string &s, double &p) {
                auto it = prices.find(s);
                if (it == prices.end()) return false;
                p = it->second;
                return true;
            };
            bool changed = true;
            CHECK(sheet.alertCheck(0.0, current, asof, changed).empty());
            CHECK(!changed);
            CHECK(sheet.listLines("AL", asof)[0] == "AL;p1;BTC;ABOVE;100;0;-");
            prices["BTC"] = 120.0;
            changed = false;
            auto fired = sheet.alertCheck(0.0, current, asof, changed);
            CHECK(fired.size() == 1);
            CHECK(fired[0] == "FIRED;PRICE;p1;BTC;ABOVE;100;120");
            CHECK(changed);
            CHECK(sheet.listLines("AL", asof)[0] == "AL;p1;BTC;ABOVE;100;1;" + iso);
            fired = sheet.alertCheck(0.0, current, asof, changed);
            CHECK(fired.empty());   // latched: the level stays true, no re-fire
            CHECK(!changed);
        }

        // 2. THE CROSSING DOWN + the boundary: sitting exactly ON the
        //    threshold is neither above nor below - here it disarms.
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("AL;p2;ETH;BELOW;200;0;-"));
            std::map<std::string, double> prices{{"ETH", 220.0}};
            auto current = [&](const std::string &s, double &p) {
                auto it = prices.find(s);
                if (it == prices.end()) return false;
                p = it->second;
                return true;
            };
            bool changed = true;
            CHECK(sheet.alertCheck(0.0, current, asof, changed).empty());
            prices["ETH"] = 180.0;
            auto fired = sheet.alertCheck(0.0, current, asof, changed);
            CHECK(fired.size() == 1);
            CHECK(fired[0] == "FIRED;PRICE;p2;ETH;BELOW;200;180");
            prices["ETH"] = 200.0;
            changed = false;
            fired = sheet.alertCheck(0.0, current, asof, changed);
            CHECK(fired.empty());
            CHECK(changed);   // 200 is not < 200: the 180 latch disarms
            CHECK(sheet.listLines("AL", asof)[0] == "AL;p2;ETH;BELOW;200;0;-");
        }

        // 3. RE-ARM by price swing: the fired alert disarms on the way
        //    out and fires AGAIN (a fresh stamp) on the way back in.
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("AL;p1;BTC;ABOVE;100;1;2026-03-01T00:00:00"));
            std::map<std::string, double> prices{{"BTC", 90.0}};
            auto current = [&](const std::string &s, double &p) {
                auto it = prices.find(s);
                if (it == prices.end()) return false;
                p = it->second;
                return true;
            };
            bool changed = false;
            CHECK(sheet.alertCheck(0.0, current, asof, changed).empty());
            CHECK(changed);   // the disarm
            CHECK(sheet.listLines("AL", asof)[0] == "AL;p1;BTC;ABOVE;100;0;-");
            std::tm asof2{};
            asof2.tm_year = 2026 - 1900;
            asof2.tm_mon = 2;
            asof2.tm_mday = 2;
            asof2.tm_hour = 11;
            prices["BTC"] = 130.0;
            changed = false;
            auto fired = sheet.alertCheck(0.0, current, asof2, changed);
            CHECK(fired.size() == 1);
            CHECK(fired[0] == "FIRED;PRICE;p1;BTC;ABOVE;100;130");
            CHECK(changed);
            CHECK(sheet.listLines("AL", asof)[0] == "AL;p1;BTC;ABOVE;100;1;2026-03-02T11:00:00");
        }

        // 4. FIRST-TRUE FIRES ONCE: an alert whose condition is already
        //    true at creation fires on the very first pass (the
        //    latched first-true rule - the rejected crossings-only
        //    alternative would never fire it).
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("AL;p3;BTC;ABOVE;50;0;-"));   // created at 120 > 50
            std::map<std::string, double> prices{{"BTC", 120.0}};
            auto current = [&](const std::string &s, double &p) {
                auto it = prices.find(s);
                if (it == prices.end()) return false;
                p = it->second;
                return true;
            };
            bool changed = false;
            auto fired = sheet.alertCheck(0.0, current, asof, changed);
            CHECK(fired.size() == 1);
            CHECK(fired[0] == "FIRED;PRICE;p3;BTC;ABOVE;50;120");
            CHECK(changed);
            CHECK(sheet.alertCheck(0.0, current, asof, changed).empty());
        }

        // 5. THE USER-EDIT RESET: a fired alert re-built by the UI with
        //    fired = 0 re-arms deliberately and fires again on the next
        //    pass (the condition still true).
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("AL;p4;ETH;BELOW;200;1;2026-03-01T09:00:00"));
            std::map<std::string, double> prices{{"ETH", 180.0}};
            auto current = [&](const std::string &s, double &p) {
                auto it = prices.find(s);
                if (it == prices.end()) return false;
                p = it->second;
                return true;
            };
            bool changed = false;
            CHECK(sheet.alertCheck(0.0, current, asof, changed).empty());   // latched
            CHECK(!changed);
            CHECK(sheet.setLine("AL;p4;ETH;BELOW;200;0;-"));   // the edit re-arms
            auto fired = sheet.alertCheck(0.0, current, asof, changed);
            CHECK(fired.size() == 1);
            CHECK(fired[0] == "FIRED;PRICE;p4;ETH;BELOW;200;180");
            CHECK(changed);
        }

        // 6. THE SYMBOL NO LONGER HELD: no current price -> skipped
        //    ENTIRELY (neither fire nor disarm, the state is untouched -
        //    no zombie notifications, no invented "last price").
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("AL;p5;XRP;ABOVE;1;1;2026-03-01T00:00:00"));
            auto noPrice = [](const std::string &, double &) { return false; };
            bool changed = true;
            CHECK(sheet.alertCheck(0.0, noPrice, asof, changed).empty());
            CHECK(!changed);
            CHECK(sheet.listLines("AL", asof)[0] == "AL;p5;XRP;ABOVE;1;1;2026-03-01T00:00:00");
        }

        // 7. THE GOAL ACHIEVED: the snapshot's GP condition on the goal
        //    metric - fires once at 100 %+, re-arms automatically when
        //    the progress drops below 1 (a goal can swing), fires again
        //    on the way back up. No user edit is involved.
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("G;g1;Fund;100;-;ASSETS"));
            auto noPrice = [](const std::string &, double &) { return false; };
            bool changed = false;
            auto fired = sheet.alertCheck(150.0, noPrice, asof, changed);   // 150% progress
            CHECK(fired.size() == 1);
            CHECK(fired[0] == "FIRED;GOAL;g1;Fund;ASSETS;150;100");
            CHECK(changed);
            CHECK(sheet.alertCheck(150.0, noPrice, asof, changed).empty());   // latched
            changed = false;
            CHECK(sheet.alertCheck(90.0, noPrice, asof, changed).empty());    // 90%: the auto re-arm
            CHECK(changed);
            CHECK(sheet.getGoalLatches().empty());
            fired = sheet.alertCheck(120.0, noPrice, asof, changed);         // 120% again
            CHECK(fired.size() == 1);
            CHECK(fired[0] == "FIRED;GOAL;g1;Fund;ASSETS;120;100");
            CHECK(changed);
        }

        // 8. THE DEGENERATE GOAL: a zero target has no crossing and no
        //    latch (the core never divides by it, GP marker aside).
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("G;g2;None;0;-;ASSETS"));
            auto noPrice = [](const std::string &, double &) { return false; };
            bool changed = true;
            CHECK(sheet.alertCheck(150.0, noPrice, asof, changed).empty());
            CHECK(!changed);
            CHECK(sheet.getGoalLatches().empty());
        }

        // 9. DELETING A GOAL REMOVES ITS LATCH (no orphan state).
        {
            BalanceSheet sheet;
            CHECK(sheet.setLine("G;g1;Fund;100;-;ASSETS"));
            auto noPrice = [](const std::string &, double &) { return false; };
            bool changed = false;
            CHECK(sheet.alertCheck(150.0, noPrice, asof, changed).size() == 1);
            CHECK(sheet.deleteEntry("Gg1"));
            CHECK(sheet.getGoalLatches().empty());
        }
    }
    SECTION("alerts: the C surface + the latch persists across incarnations");
    {
        const std::string saveDir =
                (std::filesystem::temp_directory_path() / "nf_tx_core_tests_alerts")
                        .string() +
                "/";
        std::filesystem::remove_all(saveDir);
        std::filesystem::create_directories(saveDir);

        DataHolder &dh = DataHolder::GetInstance();
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2026-03-01 09:00:00", "BTC", "1", "100", "crypto_purchase", "buy"),
        };
        CHECK(initWithData(data, Mode::CDC, "tests.log", 0));
        CHECK(dh.isInitialized());
        CHECK(save(saveDir));   // establishes the store location for the auto-save

        CHECK(sheetSet("AL;c1;BTC;ABOVE;500;0;-"));
        // The same price push as the app's path: the current cache
        // values in getCurrencies() order (EUR pinned to 1.0, like the
        // in-app price pass does).
        auto pushBtc = [](double price) {
            const auto curs = getCurrencies();
            std::vector<double> prices(curs.size(), 0.0);
            for (size_t i = 0; i < curs.size(); i++) {
                if (curs[i] == "BTC") prices[i] = price;
                if (curs[i] == "EUR") prices[i] = 1.0;
            }
            setPrice(prices);
        };
        pushBtc(600.0);
        auto fired = alertCheck();
        CHECK(fired.size() == 1);
        CHECK(fired[0].rfind("FIRED;PRICE;c1;BTC;ABOVE;500;600", 0) == 0);

        // The latch flipped: the core's own auto-save persisted it. A
        // fresh incarnation (the next app process's restore) sees the
        // fired state and does NOT re-fire at the same price.
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
        dh.loadData(saveDir);
        CHECK(sheetList("AL").size() == 1);
        CHECK(sheetList("AL")[0].rfind("AL;c1;BTC;ABOVE;500;1;", 0) == 0);
        pushBtc(600.0);
        CHECK(alertCheck().empty());

        // Edge, not level: the price drops below the threshold (the
        // disarm persists too), then rises again - the second fire.
        pushBtc(400.0);
        CHECK(alertCheck().empty());
        CHECK(sheetList("AL")[0] == "AL;c1;BTC;ABOVE;500;0;-");
        pushBtc(650.0);
        auto again = alertCheck();
        CHECK(again.size() == 1);
        CHECK(again[0].rfind("FIRED;PRICE;c1;BTC;ABOVE;500;650", 0) == 0);
        CHECK(sheetList("AL")[0].rfind("AL;c1;BTC;ABOVE;500;1;", 0) == 0);

        // The goal-achieved rides the SAME pass: the GP condition on
        // the live tracked value (1 BTC at 650 + 100 EUR buffer).
        CHECK(sheetSet("G;c2;Milestone;1;-;NET_WORTH"));
        auto goalFired = alertCheck();
        CHECK(goalFired.size() == 1);
        CHECK(goalFired[0].rfind("FIRED;GOAL;c2;Milestone;NET_WORTH;", 0) == 0);
        CHECK(alertCheck().empty());   // the goal latch

        // No zombie notifications: an alert on a symbol the core no
        // longer holds (this state holds BTC + the EUR buffer only) is
        // skipped, not fired and not disarmed.
        CHECK(sheetSet("AL;c3;DOGE;ABOVE;1;0;-"));
        CHECK(alertCheck().empty());
        CHECK(sheetList("AL").back().rfind("AL;c3;DOGE;ABOVE;1;0;", 0) == 0);

        std::filesystem::remove_all(saveDir);
    }
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
        // The EUR fee is charged on top of the outgoing fiat side
        CHECK_NEAR(tx.getToAmount(), 499.99 + 0.99, 1e-6);
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

    SECTION("BlockPit trade: crypto credited, fiat balance debited");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Trade", "EUR", "500", "BTC", "0.01")};
        auto tm = buildTmFromBlockPitLines(data);
        // BTC wallet is credited with the purchased quantity
        CHECK(tm->getWallets().count("BTC") == 1);
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), 0.01, 1e-9);
        // The fiat cost is tracked on the crypto wallet (like the CDC parser)
        CHECK_NEAR(tm->getWallets().at("BTC").getMoneySpent(), 500.0, 1e-9);
        // The fiat side is the user's exchange fiat balance: the inner EUR
        // wallet is debited (no outside-wallet bookkeeping for trades)
        CHECK(tm->getWallets().count("EUR") == 1);
        CHECK_NEAR(tm->getWallets().at("EUR").getBalance(), -500.0, 1e-9);
        CHECK(tm->getOutWallets().count("EUR") == 0);
    }

    SECTION("BlockPit sale: crypto debited, fiat proceeds credit the fiat balance");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.10.2026 21:30:23", "Bitpanda", "Trade", "BTC", "0.01", "EUR", "500")};
        auto tm = buildTmFromBlockPitLines(data);
        // The BTC wallet is debited
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), -0.01, 1e-9);
        // The sale reduces the crypto wallet's moneySpent
        CHECK_NEAR(tm->getWallets().at("BTC").getMoneySpent(), -500.0, 1e-9);
        // The fiat proceeds land in the inner EUR wallet with a positive balance
        CHECK_NEAR(tm->getWallets().at("EUR").getBalance(), 500.0, 1e-9);
        CHECK(tm->getOutWallets().count("EUR") == 0);
    }

    SECTION("BlockPit fiat balance nets transfers, deposits and trades");
    {
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("01.01.2026 21:30:23", "Bitpanda", "Non-Taxable In", "", "", "EUR", "1000"),
                                      bpLine("02.01.2026 21:30:23", "Bitpanda", "Trade", "EUR", "600", "BTC", "0.006"),
                                      bpLine("03.01.2026 21:30:23", "Bitpanda", "Trade", "BTC", "0.006", "EUR", "700"),
                                      bpLine("04.01.2026 21:30:23", "Bitpanda", "Withdrawal", "EUR", "300", "", "")};
        auto tm = buildTmFromBlockPitLines(data);
        // 1000 in - 600 spent + 700 proceeds - 300 withdrawn = 800
        CHECK_NEAR(tm->getWallets().at("EUR").getBalance(), 800.0, 1e-9);
        // Money spent stays the net purchase cost (the fiat rows carry no native amount)
        CHECK_NEAR(tm->getWallets().at("BTC").getMoneySpent(), 600.0 - 700.0, 1e-9);
        // Fiat rows must not create a duplicate outside EUR wallet
        CHECK(tm->getOutWallets().count("EUR") == 0);
    }

    SECTION("BlockPit fees reduce the paid side; fee tokens settle their own wallet");
    {
        // Purchase with an EUR fee: charged on top of the outgoing fiat side
        auto tx = parseBlockPit(bpLine("01.10.2026 21:30:23", "Kraken", "Trade", "EUR", "50", "BTC", "0.001", "EUR", "1.50"));
        CHECK_NEAR(tx.getToAmount(), 50.0 + 1.50, 1e-9);
        CHECK_NEAR(tx.getNativeAmount(), 50.0 + 1.50, 1e-9);
        CHECK_NEAR(tx.getAmount(), 0.001, 1e-9);

        // Deposit with an EUR fee (the 51 EUR / 1 EUR Binance case):
        // deducted from what arrives
        auto tx2 = parseBlockPit(bpLine("03.04.2022 12:00:00", "Binance", "Non-Taxable In", "", "", "EUR", "51", "EUR", "1"));
        CHECK_NEAR(tx2.getAmount(), 50.0, 1e-9);

        // A KFEE fee is not a side of the trade: row amounts stay untouched,
        // the fee is only recorded for the manager to settle
        auto tx3 = parseBlockPit(bpLine("01.10.2026 21:30:23", "Kraken", "Trade", "EUR", "50", "BTC", "0.001", "KFEE", "28.51"));
        CHECK_NEAR(tx3.getToAmount(), 50.0, 1e-9);
        CHECK_NEAR(tx3.getAmount(), 0.001, 1e-9);
        CHECK(tx3.getFeeAsset() == "KFEE");

        // Manager: the KFEE gift (held position, not a bonus) minus the KFEE
        // fees leaves exactly the remainder that was held
        std::vector<std::string> data{
                blockPitHeader(),
                bpLine("13.10.2023 11:13:00", "Kraken", "Gift Received", "", "", "KFEE", "10000"),
                bpLine("01.11.2023 21:30:23", "Kraken", "Trade", "EUR", "50", "BTC", "0.001", "KFEE", "28.51"),
                bpLine("02.11.2023 21:30:23", "Kraken", "Trade", "EUR", "100", "BTC", "0.002", "KFEE", "14.82")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK_NEAR(tm->getWallets().at("KFEE").getBalance(), 10000.0 - 28.51 - 14.82, 1e-9);
        CHECK_NEAR(tm->getWallets().at("EUR").getBalance(), -150.0, 1e-9);
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
                                      // Real standalone "Fee" label rows carry the fee in the
                                      // outgoing amount only (no fee columns)
                                      bpLine("02.10.2026 21:30:23", "Bitpanda", "Fee", "KFEE", "28.51", "", "", "", ""),
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

    SECTION("BlockPit deposit credits the asset wallet and debits the outside ledger");
    {
        // A synthetic row in the shape of a chain deposit into a cold
        // wallet (the committed fixture carries the same shape); the pinned
        // booking is the expectation: the asset wallet is credited, the
        // outside ledger is debited by the same amount, and the row is
        // wired to the asset wallet's id.
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("15.03.2025 10:30:00", "Cold Wallet", "Deposit", "", "", "BTC", "0.021547", "", "", "", "", "Manual", "Manual")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK(tm->getWallets().count("BTC") == 1);
        CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), 0.021547, 1e-12);
        CHECK_NEAR(tm->getWallets().at("BTC").getMoneySpent(), 0.0, 1e-12);
        CHECK(tm->getOutWallets().count("BTC") == 1);
        CHECK_NEAR(tm->getOutWallets().at("BTC").getBalance(), -0.021547, 1e-12);
        const auto &btc = tm->getWallets().at("BTC");
        CHECK(btc.getTransactions().size() == 1);
        CHECK(btc.getTransactions().front().getWalletId() == btc.getWalletId());
    }

    SECTION("BlockPit fiat deposit stays in the wallet map (no outside entry)");
    {
        // A fiat deposit is booked in the inner fiat wallet only - a separate
        // outside bookkeeping wallet would double-count the row in the list.
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("03.04.2025 09:00:00", "Bank", "Deposit", "", "", "EUR", "51")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK(tm->getWallets().count("EUR") == 1);
        CHECK_NEAR(tm->getWallets().at("EUR").getBalance(), 51.0, 1e-9);
        CHECK(tm->getOutWallets().count("EUR") == 0);
    }

    SECTION("BlockPit deposit fee in the incoming asset nets the credit");
    {
        // 51 EUR in, 1 EUR fee, 50 nets into the account: the fee in the
        // incoming asset nets the credit.
        std::vector<std::string> data{blockPitHeader(),
                                      bpLine("03.04.2022 12:00:00", "Binance", "Deposit", "", "", "EUR", "51", "EUR", "1")};
        auto tm = buildTmFromBlockPitLines(data);
        CHECK_NEAR(tm->getWallets().at("EUR").getBalance(), 50.0, 1e-9);
        CHECK(tm->getOutWallets().count("EUR") == 0);
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
        // The native books on the outside wallet only - the inner count
        // would double the "money spent" card.
        CHECK_NEAR(tm->getWallets().at("BTC").getMoneySpent(), 0.0, 1e-9);
        CHECK_NEAR(tm->getOutWallets().at("BTC").getMoneySpent(), 100.0, 1e-9);
    }

    SECTION("CDC withdrawal keeps the inner row with a zeroed native");
    {
        // Witness shape (signed-negative amount, positive native): the inner
        // list keeps the row (balance flow + membership) with native 0, the
        // outside list keeps the parsed native, and the master row keeps it
        // too (the UI transaction list is master-based).
        std::vector<std::string> data{CDC_HEADER,
                                      cdcLine("2023-04-01 12:34:56", "CRO", "-38.90", "14.19", "crypto_withdrawal")};
        auto tm = buildTmFromCdcLines(data);
        tm->calculateWalletBalances();
        CHECK_NEAR(tm->getTotalMoneySpent(), 0.0, 1e-9);
        CHECK_NEAR(tm->getWallets().at("CRO").getBalance(), -38.90, 1e-9);
        CHECK_NEAR(tm->getOutWallets().at("CRO").getBalance(), 38.90, 1e-9);
        CHECK_NEAR(tm->getWallets().at("CRO").getMoneySpent(), 0.0, 1e-9);
        CHECK_NEAR(tm->getOutWallets().at("CRO").getMoneySpent(), -14.19, 1e-9);
        auto innerRows = tm->getWallets().at("CRO").getTransactions();
        CHECK(innerRows.size() == 1);
        CHECK_NEAR(innerRows.front().getAmount(), -38.90, 1e-12);
        CHECK_NEAR(innerRows.front().getNativeAmount(), 0.0, 1e-12);
        auto outerRows = tm->getOutWallets().at("CRO").getTransactions();
        CHECK(outerRows.size() == 1);
        CHECK_NEAR(outerRows.front().getNativeAmount(), 14.19, 1e-12);
        CHECK_NEAR(tm->getTransactions().front().getNativeAmount(), 14.19, 1e-12);
        // The row-based monthly series: the zeroed inner row contributes 0.
        auto series = tm->getMoneySpentSeries();
        CHECK(series.size() == 1);
        const std::string &row = series.front();
        CHECK_NEAR(std::stold(row.substr(row.find(';') + 1)), 0.0, 1e-12);
    }

    SECTION("CDC deposit books the same outside-only native");
    {
        std::vector<std::string> data{CDC_HEADER,
                                      cdcLine("2023-04-01 12:34:56", "CRO", "10.0", "2.0", "crypto_deposit")};
        auto tm = buildTmFromCdcLines(data);
        CHECK_NEAR(tm->getWallets().at("CRO").getMoneySpent(), 0.0, 1e-9);
        CHECK_NEAR(tm->getOutWallets().at("CRO").getMoneySpent(), -2.0, 1e-9);
        CHECK_NEAR(tm->getWallets().at("CRO").getBalance(), 10.0, 1e-9);
        CHECK_NEAR(tm->getOutWallets().at("CRO").getBalance(), -10.0, 1e-9);
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

static void testMoneySpentSeries() {
    SECTION("getMoneySpentSeries buckets monthly and sums to the total");
    auto tm = buildTmFromCdcLines({
            CDC_HEADER,
            cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase"),
            cdcLine("2023-04-15 12:34:56", "ETH", "2", "50", "crypto_purchase"),
            cdcLine("2023-05-02 12:34:56", "BTC", "3", "75", "crypto_purchase"),
    });
    auto series = tm->getMoneySpentSeries();
    CHECK(series.size() == 2);
    CHECK(series.front().rfind("2023-04;", 0) == 0);
    CHECK(series.back().rfind("2023-05;", 0) == 0);
    long double sum = 0;
    for (const auto &entry: series) {
        const auto semicolon = entry.find(';');
        if (semicolon == std::string::npos) continue;
        sum += std::stold(entry.substr(semicolon + 1));
    }
    // Same per-wallet accounting the card total uses (inner wallets, EUR skipped).
    long double direct = 0;
    for (const auto &entry: tm->getWallets()) {
        if (entry.first == "EUR") continue;
        direct += static_cast<long double>(entry.second.getMoneySpent());
    }
    CHECK(std::abs(sum - direct) < 1e-6L);
}

static void testDailySeries() {
    SECTION("getDailySeries replays the ledgers into the four card-consistent series");
    // Signed CDC amounts: the withdrawal row debits the inner BTC wallet and
    // credits the OUTSIDE wallet (out of the inner-wallet scope). The
    // interest row moves into the bonus balance via setAmountToAmountBonus.
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-03-05 12:00:00", "BTC", "1", "100", "crypto_purchase"),
            cdcLine("2023-03-20 12:00:00", "ETH", "2", "50", "crypto_purchase"),
            cdcLine("2023-04-01 12:00:00", "ETH", "3", "75", "crypto_purchase"),
            cdcLine("2023-04-10 12:00:00", "BTC", "-1", "-20", "crypto_withdrawal"),
            cdcLine("2023-04-12 12:00:00", "BTC", "0.5", "10", "crypto_earn_interest_paid"),
    };
    auto tm = buildTmFromCdcLines(data);
    const auto &curs = tm->getCurrencies();
    std::vector<double> prices(curs.size(), 0.0);
    for (size_t i = 0; i < curs.size(); i++) {
        if (curs[i] == "BTC") prices[i] = 100.0;
        if (curs[i] == "ETH") prices[i] = 50.0;
    }
    tm->setPrices(prices);
    tm->calculateWalletBalances();

    auto rows = tm->getDailySeries();
    CHECK(rows.size() == 20); // 5 active days x 4 series
    // std::to_string doubles: 6 fixed decimals (same convention as
    // getMoneySpentSeries).
    CHECK(rows.front() == "spent;2023-03-05;100.000000");

    // Collect the per-day points and check the exact expected values.
    std::map<std::string, long double> spent, value, pl, bonus;
    std::string currentDay;
    bool firstDayRow = true;
    size_t orderInDay = 0;
    for (const auto &row: rows) {
        // "series;YYYY-MM-DD;value"
        auto i1 = row.find(';');
        auto i2 = row.find(';', i1 + 1);
        CHECK(i1 != std::string::npos && i2 != std::string::npos);
        std::string series = row.substr(0, i1);
        std::string key = row.substr(i1 + 1, i2 - i1 - 1);
        long double v = std::stold(row.substr(i2 + 1));
        std::map<std::string, long double> *target = nullptr;
        size_t order = 1; // spent=0, value=1, pl=2, bonus=3
        if (series == "spent") {
            target = &spent;
            order = 0;
        } else if (series == "value") {
            target = &value;
        } else if (series == "pl") {
            target = &pl;
            order = 2;
        } else if (series == "bonus") {
            target = &bonus;
            order = 3;
        } else {
            CHECK(false);
            continue;
        }
        if (key != currentDay) {
            currentDay = key;
            firstDayRow = true;
        }
        // The four rows of one day keep spent..bonus order and days rise.
        CHECK(firstDayRow ? order == 0 : order > orderInDay);
        firstDayRow = false;
        orderInDay = order;
        (*target)[key] = v;
    }
    CHECK(spent["2023-03-05"]== 100.0L);
    CHECK(value["2023-03-05"]== 100.0L); // 1 BTC x 100
    CHECK(pl["2023-03-05"]== 0.0L);
    CHECK(bonus["2023-03-05"]== 0.0L);
    CHECK(spent["2023-03-20"]== 50.0L);
    CHECK(value["2023-03-20"]== 200.0L); // 1 BTC + 2 ETH
    CHECK(pl["2023-03-20"]== 50.0L); // 200 - 150
    CHECK(spent["2023-04-01"]== 75.0L);
    CHECK(value["2023-04-01"]== 350.0L); // 1 BTC + 5 ETH
    // The withdrawal's native books on the outside wallet only: the day
    // still emits (the balance delta is real) with zero spent flow.
    CHECK(spent["2023-04-10"]== 0.0L);
    CHECK(value["2023-04-10"]== 250.0L); // 0 BTC + 5 ETH
    CHECK(pl["2023-04-10"]== 25.0L); // 250 - (100 + 50 + 75 + 0)
    CHECK(spent["2023-04-12"]== 10.0L);
    CHECK(value["2023-04-12"]== 300.0L); // 0.5 BTC + 5 ETH
    CHECK(pl["2023-04-12"]== 65.0L); // 300 - 235
    CHECK(bonus["2023-04-12"]== 50.0L); // 0.5 BTC x 100

    // Telescoping: the series must land exactly on the card totals.
    long double spentSum = 0;
    for (const auto &entry: spent) spentSum += entry.second;
    CHECK_NEAR(static_cast<double>(spentSum), tm->getTotalMoneySpent(), 1e-9);
    CHECK_NEAR(static_cast<double>(value["2023-04-12"]),
               tm->getTotalValueOfAssets(), 1e-9);
    CHECK_NEAR(static_cast<double>(bonus["2023-04-12"]),
               tm->getTotalBonus(), 1e-9);
    CHECK_NEAR(static_cast<double>(pl["2023-04-12"]),
               tm->getTotalValueOfAssets() - tm->getTotalMoneySpent(), 1e-9);

    // An unsorted file yields the same daily series (chronological replay):
    {
        std::vector<std::string> shuffled = data; // header at 0, lines at 1..5
        // Reorder the data lines so the file order no longer matches the dates.
        std::swap(shuffled[3], shuffled[5]);
        std::swap(shuffled[2], shuffled[4]);
        auto tmShuffled = buildTmFromCdcLines(shuffled); // same currencies
        tmShuffled->setPrices(prices);
        tmShuffled->calculateWalletBalances();
        CHECK(tmShuffled->getDailySeries() == rows);
    }

    // An empty manager yields no series lines.
    {
        auto empty = std::make_unique<TransactionManager>();
        CHECK(empty->getDailySeries().empty());
    }
}

static void testSubDailySeries() {
    SECTION("sub-daily series bucket into fixed slots and carry state across the fromEpoch cut");
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-04-01 06:23:00", "BTC", "1", "100", "crypto_purchase"),
            cdcLine("2023-04-01 11:59:00", "BTC", "1", "100", "crypto_purchase"),
            cdcLine("2023-04-01 12:01:00", "ETH", "2", "50", "crypto_purchase"),
            cdcLine("2023-04-02 00:30:00", "BTC", "-1", "-50", "crypto_withdrawal"),
    };
    auto tm = buildTmFromCdcLines(data);
    tm->calculateWalletBalances();

    // 6h slots: 06:23 + 11:59 both floor to the 06:00-12:00 slot. The
    // withdrawal slot carries a balance delta but zero spent flow (its
    // native books outside), so it stays in the wallet series but is
    // skipped by the spent series.
    {
        auto spent = tm->getSubDailySpentSeries(6 * 3600);
        CHECK(spent.size() == 2);
        CHECK(spent[0] == "2023-04-01 06:00;200.000000");
        CHECK(spent[1] == "2023-04-01 12:00;50.000000");
    }
    {
        auto wallet = tm->getSubDailyWalletSeries(6 * 3600);
        // One line per (slot, currency) that changed; running token state.
        CHECK(wallet.size() == 3);
        CHECK(wallet[0] == "BTC;2023-04-01 06:00;2.000000;0.000000");
        CHECK(wallet[1] == "ETH;2023-04-01 12:00;2.000000;0.000000");
        CHECK(wallet[2] == "BTC;2023-04-02 00:00;1.000000;0.000000");
    }

    // 15m slots: 06:23 -> 06:15, 11:59 -> 11:45, 12:01 -> 12:00, 00:30 -> 00:30.
    {
        auto spent = tm->getSubDailySpentSeries(15 * 60);
        CHECK(spent.size() == 3);
        CHECK(spent[0] == "2023-04-01 06:15;100.000000");
        CHECK(spent[1] == "2023-04-01 11:45;100.000000");
        CHECK(spent[2] == "2023-04-01 12:00;50.000000");
    }

    // fromEpoch cut: earlier slots are NOT emitted, but they still fed the
    // running state. The LIMITED wallet window additionally opens with the
    // state carried in from before the cut (BTC held 2.0), at the
    // fromEpoch slot key.
    {
        std::tm cut = {};
        cut.tm_year = 2023 - 1900; cut.tm_mon = 3; cut.tm_mday = 1;
        cut.tm_hour = 12; cut.tm_min = 0; cut.tm_sec = 0;
        const long long from = timegm(&cut);
        auto spent = tm->getSubDailySpentSeries(6 * 3600, from);
        CHECK(spent.size() == 1);
        CHECK(spent[0] == "2023-04-01 12:00;50.000000");
        auto wallet = tm->getSubDailyWalletSeries(6 * 3600, from);
        CHECK(wallet.size() == 3);
        CHECK(wallet[0] == "BTC;2023-04-01 12:00;2.000000;0.000000"); // opening line
        CHECK(wallet[1] == "ETH;2023-04-01 12:00;2.000000;0.000000");
        CHECK(wallet[2] == "BTC;2023-04-02 00:00;1.000000;0.000000");
    }

    // Degenerate input: invalid window sizes give no rows (and no crash).
    CHECK(tm->getSubDailySpentSeries(0).empty());
    CHECK(tm->getSubDailyWalletSeries(-5).empty());
}

static void testSubDailyOpeningLine() {
    SECTION("limited sub-daily window opens with the state carried in from before the cut");
    // Main dataset (6h slots, cut 2023-03-15 00:00 UTC):
    //  ETH bought well before the cut, then CLOSED before the cut,
    //  BTC bought before the cut, one withdrawal INSIDE the window,
    //  LUNA created inside the window (earn, balance+bonus).
    auto buildMain = []() {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-02-10 09:00:00", "ETH", "3", "60", "crypto_purchase"),
                cdcLine("2023-03-01 10:00:00", "BTC", "2", "200", "crypto_purchase"),
                cdcLine("2023-03-05 22:00:00", "ETH", "-3", "-60", "crypto_withdrawal"),
                cdcLine("2023-03-16 07:00:00", "BTC", "-1", "-100", "crypto_withdrawal"),
                cdcLine("2023-03-18 12:30:00", "LUNA", "2", "4", "crypto_earn_interest_paid"),
        };
        auto tm = buildTmFromCdcLines(data);
        tm->calculateWalletBalances();
        return tm;
    };
    auto cutAt = [](int year, int month, int day) {
        std::tm cut = {};
        cut.tm_year = year - 1900; cut.tm_mon = month - 1; cut.tm_mday = day;
        cut.tm_hour = 0; cut.tm_min = 0; cut.tm_sec = 0;
        return timegm(&cut);
    };
    const long long march15 = cutAt(2023, 3, 15);
    const long long march01 = cutAt(2023, 3, 1);

    // Held position (BTC) gets the opening line at the fromEpoch slot key;
    // the changed slot inside the window continues from it (2 - 1 = 1).
    // The closed currency (ETH) and the zero-at-cut currency (LUNA) do not
    // get opening lines.
    {
        auto tm = buildMain();
        auto rows = tm->getSubDailyWalletSeries(6 * 3600, march15);
        CHECK(rows.size() == 3);
        CHECK(rows[0] == "BTC;2023-03-15 00:00;2.000000;0.000000");
        CHECK(rows[1] == "BTC;2023-03-16 06:00;1.000000;0.000000");
        CHECK(rows[2] == "LUNA;2023-03-18 12:00;2.000000;2.000000");
    }

    // A transaction EXACTLY at the cut: the opening line is the state
    // BEFORE it (4.0), the changed-slot line in the same slot is after (3.0).
    {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-03-01 10:00:00", "BTC", "4", "120", "crypto_purchase"),
                cdcLine("2023-03-15 00:00:00", "BTC", "-1", "-30", "crypto_withdrawal"),
        };
        auto tm = buildTmFromCdcLines(data);
        auto rows = tm->getSubDailyWalletSeries(6 * 3600, march15);
        CHECK(rows.size() == 2);
        CHECK(rows[0] == "BTC;2023-03-15 00:00;4.000000;0.000000");
        CHECK(rows[1] == "BTC;2023-03-15 00:00;3.000000;0.000000");
    }

    // Zero position at the window start (first activity AFTER the cut):
    // no opening line, the plain changed-slot emission only.
    {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-03-16 07:00:00", "SOL", "2", "40", "crypto_purchase"),
        };
        auto tm = buildTmFromCdcLines(data);
        auto rows = tm->getSubDailyWalletSeries(6 * 3600, march15);
        CHECK(rows.size() == 1);
        CHECK(rows[0] == "SOL;2023-03-16 06:00;2.000000;0.000000");
    }

    // Fully stale export (everything before the cut): the opening state
    // stands alone at the fromEpoch slot key.
    {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-01-05 09:15:00", "BTC", "1.5", "50", "crypto_purchase"),
        };
        auto tm = buildTmFromCdcLines(data);
        auto rows = tm->getSubDailyWalletSeries(6 * 3600, march01);
        CHECK(rows.size() == 1);
        CHECK(rows[0] == "BTC;2023-03-01 00:00;1.500000;0.000000");
    }

    // Bonus-only position (earned, then the balance drawn down to zero):
    // the opening line carries the bonus with a zero balance.
    {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-02-20 09:00:00", "LUNA", "2", "4", "crypto_earn_interest_paid"),
                cdcLine("2023-02-21 10:00:00", "LUNA", "-2", "-4", "crypto_withdrawal"),
        };
        auto tm = buildTmFromCdcLines(data);
        auto rows = tm->getSubDailyWalletSeries(6 * 3600, march01);
        CHECK(rows.size() == 1);
        CHECK(rows[0] == "LUNA;2023-03-01 00:00;0.000000;2.000000");
    }

    // Backward compatibility: fromEpoch = 0 (unlimited) emits NO opening
    // line - the plain changed-slot series is byte-identical to before.
    {
        auto tm = buildMain();
        auto rows = tm->getSubDailyWalletSeries(6 * 3600);
        CHECK(rows.size() == 5);
        CHECK((rows == std::vector<std::string>({
                "ETH;2023-02-10 06:00;3.000000;0.000000",
                "BTC;2023-03-01 06:00;2.000000;0.000000",
                "ETH;2023-03-05 18:00;0.000000;0.000000",
                "BTC;2023-03-16 06:00;1.000000;0.000000",
                "LUNA;2023-03-18 12:00;2.000000;2.000000",
        })));
    }
}

static void testDailyWalletSeries() {
    SECTION("getDailyWalletSeries: running token amounts per inner wallet (buffer-inclusive scope)");
    // The viban row is written raw (cdcLine cannot fill the To columns): it
    // hits the inner EUR wallet (a first-class line now: the per-wallet
    // family is buffer-inclusive) and the CRO wallet.
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-04-01 12:34:56", "BTC", "1.0", "100", "crypto_purchase"),
            cdcLine("2023-04-02 09:00:00", "ETH", "2.0", "50", "crypto_purchase"),
            cdcLine("2023-04-03 08:00:00", "LUNA", "3.0", "5", "crypto_earn_interest_paid"),
            cdcLine("2023-04-05 09:00:00", "BTC", "-0.5", "100", "crypto_withdrawal"),
            "2023-04-06 11:00:00,eur buy,EUR,-30,CRO,1.5,USD,30,30,viban_purchase,",
    };
    auto tm = buildTmFromCdcLines(data);
    auto rows = tm->getDailyWalletSeries();
    // One line per (wallet, active day), chronological, running values;
    // per day the currencies are alphabetical (CRO < EUR on the 06th).
    CHECK((rows == std::vector<std::string>({
            "BTC;2023-04-01;1.000000;0.000000",
            "ETH;2023-04-02;2.000000;0.000000",
            "LUNA;2023-04-03;3.000000;3.000000",
            "BTC;2023-04-05;0.500000;0.000000",   // 1.0 - 0.5, carried over the gap
            "CRO;2023-04-06;1.500000;0.000000",
            "EUR;2023-04-06;-30.000000;0.000000"   // the buffer's fiat leg
    })));
    // Telescoping: the last running point of each currency equals its
    // wallet (the EUR side included - that is the buffer-inclusive scope).
    std::map<std::string, long double> lastBal, lastBonus;
    for (const auto &row: rows) {
        auto p1 = row.find(';');
        auto p2 = row.find(';', p1 + 1);
        auto p3 = row.find(';', p2 + 1);
        lastBal[row.substr(0, p1)] = std::stold(row.substr(p2 + 1, p3 - p2 - 1));
        lastBonus[row.substr(0, p1)] = std::stold(row.substr(p3 + 1));
    }
    for (const auto &entry: tm->getWallets()) {
        CHECK_NEAR(lastBal[entry.first], entry.second.getBalance(), 1e-12);
        CHECK_NEAR(lastBonus[entry.first], entry.second.getBonusBalance(), 1e-12);
    }
    // REGRESSION GUARD on the scope split: the 4-line TOTAL family on the
    // SAME state keeps its pinned crypto-only bytes (the buffer flow never
    // leaks into spent/value/pl/bonus) - these are the pre-split bytes of
    // exactly this fixture.
    CHECK((tm->getDailySeries() == std::vector<std::string>({
            "spent;2023-04-01;100.000000",
            "value;2023-04-01;75352.000000",
            "pl;2023-04-01;75252.000000",
            "bonus;2023-04-01;0.000000",
            "spent;2023-04-02;50.000000",
            "value;2023-04-02;80113.820000",
            "pl;2023-04-02;79963.820000",
            "bonus;2023-04-02;0.000000",
            "spent;2023-04-03;5.000000",
            "value;2023-04-03;80113.820000",
            "pl;2023-04-03;79958.820000",
            "bonus;2023-04-03;0.000000",
            "spent;2023-04-05;0.000000",
            "value;2023-04-05;42437.820000",
            "pl;2023-04-05;42282.820000",
            "bonus;2023-04-05;0.000000",
            "spent;2023-04-06;30.000000",
            "value;2023-04-06;42437.907773",
            "pl;2023-04-06;42252.907773",
            "bonus;2023-04-06;0.000000"
    })));
    {
        auto emptyTm = std::make_unique<TransactionManager>();
        CHECK(emptyTm->getDailyWalletSeries().empty());
    }
}

static void testDailyWalletSeriesEurScope() {
    SECTION("per-wallet daily series: the EUR buffer line is first-class (multi-day running balance)");
    // Multi-day buffer (the exchange-fiat side of internal movements):
    // 1000 in, 600 spent on 0.006 BTC, 700 sale proceeds back, 300
    // withdrawn -> hand-checked running EUR 1000 / 400 / 1100 / 800, with
    // the BTC side 0.006 / 0.0 over days 2..3.
    std::vector<std::string> data{
            blockPitHeader(),
            bpLine("01.01.2026 21:30:23", "Bitpanda", "Non-Taxable In", "", "", "EUR", "1000"),
            bpLine("02.01.2026 21:30:23", "Bitpanda", "Trade", "EUR", "600", "BTC", "0.006"),
            bpLine("03.01.2026 21:30:23", "Bitpanda", "Trade", "BTC", "0.006", "EUR", "700"),
            bpLine("04.01.2026 21:30:23", "Bitpanda", "Withdrawal", "EUR", "300", "", ""),
    };
    auto tm = buildTmFromBlockPitLines(data);
    CHECK((tm->getDailyWalletSeries() == std::vector<std::string>({
            "EUR;2026-01-01;1000.000000;0.000000",
            "BTC;2026-01-02;0.006000;0.000000",
            "EUR;2026-01-02;400.000000;0.000000",
            "BTC;2026-01-03;0.000000;0.000000",
            "EUR;2026-01-03;1100.000000;0.000000",
            "EUR;2026-01-04;800.000000;0.000000"})));
    // The series lands on the wallet balances (the buffer included).
    CHECK_NEAR(tm->getWallets().at("EUR").getBalance(), 800.0, 1e-9);
    CHECK_NEAR(tm->getWallets().at("BTC").getBalance(), 0.0, 1e-9);
}

static void testSubDailyWalletSeriesEurScope() {
    SECTION("sub-daily per-wallet series: the EUR line + the limited-window opening carries the buffer");
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-03-01 12:00:00", "BTC", "1.0", "100", "crypto_purchase"),
            "2023-03-02 12:00:00,eur buy,EUR,-30,CRO,1.5,USD,30,30,viban_purchase,",
    };
    auto tm = buildTmFromCdcLines(data);
    // Unlimited: the EUR wallet's changed slot is an emitted line (stable
    // per-slot alphabetical order: CRO before EUR).
    CHECK((tm->getSubDailyWalletSeries(6 * 3600) == std::vector<std::string>({
            "BTC;2023-03-01 12:00;1.000000;0.000000",
            "CRO;2023-03-02 12:00;1.500000;0.000000",
            "EUR;2023-03-02 12:00;-30.000000;0.000000"})));
    // Fully stale limited window (all activity before the cut): only the
    // opening lines, and they carry the EUR position the window inherits.
    const long long from = 1677996000LL; // 2023-03-05 06:00:00 UTC (grid-aligned)
    CHECK((tm->getSubDailyWalletSeries(6 * 3600, from) == std::vector<std::string>({
            "BTC;2023-03-05 06:00;1.000000;0.000000",
            "CRO;2023-03-05 06:00;1.500000;0.000000",
            "EUR;2023-03-05 06:00;-30.000000;0.000000"})));
}

static void testDailyWalletSeriesEdges() {
    SECTION("getDailyWalletSeries edges: single day, same-day ties, zero-net day, no activity");
    // Single-day wallet: exactly one point, the inception-referenced value.
    {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-05-10 08:00:00", "BTC", "2.5", "100", "crypto_purchase"),
        };
        auto tm = buildTmFromCdcLines(data);
        CHECK((tm->getDailyWalletSeries() == std::vector<std::string>({
                "BTC;2023-05-10;2.500000;0.000000"})));
    }
    // Same-day ties: several currencies changing on one day keep a
    // deterministic row order (day-major, currencies alphabetical within
    // the day) - re-runs emit byte-identical rows, and the BTC day total
    // is the running sum of its same-day deltas.
    {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-05-10 08:00:00", "BTC", "1.0", "100", "crypto_purchase"),
                cdcLine("2023-05-10 09:00:00", "ETH", "2.0", "50", "crypto_purchase"),
                cdcLine("2023-05-10 10:00:00", "BTC", "0.5", "50", "crypto_purchase"),
        };
        auto tm = buildTmFromCdcLines(data);
        auto first = tm->getDailyWalletSeries();
        CHECK((first == tm->getDailyWalletSeries()));   // stable across re-runs
        CHECK((first == std::vector<std::string>({
                "BTC;2023-05-10;1.500000;0.000000",
                "ETH;2023-05-10;2.000000;0.000000"})));
    }
    // Zero-net day: a day that returns the balance to the previous running
    // total emits nothing ("totals changed" is the emission contract).
    {
        std::vector<std::string> data{
                CDC_HEADER,
                cdcLine("2023-05-10 08:00:00", "BTC", "1.0", "100", "crypto_purchase"),
                cdcLine("2023-05-10 09:00:00", "BTC", "-1.0", "100", "crypto_withdrawal"),
        };
        auto tm = buildTmFromCdcLines(data);
        CHECK(tm->getDailyWalletSeries().empty());
    }
    // No activity at all: a manager that processes without any
    // transactions (setTransactions refuses the empty set, so this goes
    // through processTransactions directly) has no wallets and no series.
    {
        TransactionManager empty;
        empty.processTransactions();
        CHECK(empty.getWallets().empty());
        CHECK(empty.getDailyWalletSeries().empty());
    }
}

static void testParseBudget() {
    // B22: a hard wall-clock budget must abort a pathological parse instead of
    // wedging the single JNI thread. Negative budget = already expired.
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase"),
            cdcLine("2023-04-02 12:34:56", "ETH", "2", "200", "crypto_purchase"),
    };
    // Expired budget: the parse loop must abort before taking any line
    // (the empty result then surfaces as a clean parse failure upstream).
    TransactionManager::setParseBudgetMs(-1);
    CHECK(TransactionManager::parseBudgetExceeded());
    TransactionParser parserAborted(data);
    parserAborted.parseFromCsv(Mode::CDC);
    CHECK(parserAborted.getTransactions().empty());
    // The cut becomes queryable: both data lines were never parsed (the
    // header is stripped from the loop input before the first line).
    CHECK(TransactionManager::wasLastParseCutByBudget());
    CHECK(TransactionManager::lastParseUnprocessedRows() == 2);

    // A fresh parse unit (the positive budget the app always sends) resets
    // the result state: the normal parse of the SAME fixture reports no
    // cut - the flags never leak across parses.
    TransactionManager::setParseBudgetMs(120000);
    CHECK(!TransactionManager::parseBudgetExceeded());
    auto tmOk = buildTmFromCdcLines(data);
    CHECK(tmOk->getTransactions().size() == 2);
    CHECK(tmOk->getWallets().size() == 2);
    CHECK(!TransactionManager::wasLastParseCutByBudget());
    CHECK(TransactionManager::lastParseUnprocessedRows() == 0);
}

static void testXmlRoundTrip() {
    SECTION("XML round trip: serializeToXml -> deserializeFromXml");
    // The deserialization side used to be unexercised (only the ASan
    // dangling-node serialization check existed); both structs must come
    // back field-for-field, including the awkward ones.
    TransactionData tx;
    tx.transactionId = 12;
    tx.walletId = 3;
    tx.fromWalletId = 5;
    tx.description = "round trip test";
    tx.transactionDate = TimestampConverter::stringToTm("2024-02-29 01:02:03");
    tx.currencyType = "BTC";
    tx.toCurrencyType = "EUR";
    tx.feeAsset = "ETH";
    tx.amount = -0.5L;             // signed (debit) survives
    tx.toAmount = 99999.987654L;   // 6 decimals = std::to_string precision
    tx.nativeAmount = 12.345678L;
    tx.amountBonus = 0.0L;
    tx.transactionTypeOrdinal = 7;
    tx.transactionTypeString = "crypto_sale";
    tx.transactionHash = "abcd1234";
    tx.isOutsideTransaction = true;
    tx.excludedFromTotals = true;
    tx.notes = "";                 // empty field: prints as <notes/>, parses back empty
    std::string xml = tx.serializeToXml();

    TransactionData back;
    back.deserializeFromXml(xml);
    CHECK(back.transactionId == 12);
    CHECK(back.walletId == 3);
    CHECK(back.fromWalletId == 5);
    CHECK(back.description == tx.description);
    CHECK(back.transactionDate.tm_year == tx.transactionDate.tm_year);
    CHECK(back.transactionDate.tm_mon == tx.transactionDate.tm_mon);
    CHECK(back.transactionDate.tm_mday == 29);
    CHECK(back.transactionDate.tm_hour == 1);
    CHECK(back.transactionDate.tm_min == 2);
    CHECK(back.transactionDate.tm_sec == 3);
    CHECK(back.currencyType == "BTC");
    CHECK(back.toCurrencyType == "EUR");
    CHECK(back.feeAsset == "ETH");
    CHECK(back.amount == -0.5L);
    CHECK(back.toAmount == 99999.987654L);
    CHECK(back.nativeAmount == 12.345678L);
    CHECK(back.amountBonus == 0.0L);
    CHECK(back.transactionTypeOrdinal == 7);
    CHECK(back.transactionTypeString == "crypto_sale");
    CHECK(back.transactionHash == "abcd1234");
    CHECK(back.isOutsideTransaction);
    CHECK(back.excludedFromTotals);
    CHECK(back.notes.empty());

    // A leading XML declaration must be stripped before the field scan.
    TransactionData back2;
    back2.deserializeFromXml(std::string("<?xml version='1.0' encoding='UTF-8'?>") + xml);
    CHECK(back2.transactionId == 12);
    CHECK(back2.transactionDate.tm_mday == 29);
    CHECK(back2.amount == -0.5L);

    WalletData w0;
    w0.walletId = 4;
    w0.currencyType = "USDT";
    w0.balance = -12.5;
    w0.nativeBalance = 300.125;
    w0.bonusBalance = 0.012345;
    w0.moneySpent = 777.75;
    w0.isOutsideWallet = false;
    w0.notes = "n";
    WalletData w1;
    WalletData::deserializeFromXml(WalletData::serializeToXml(w0), w1);
    CHECK(w1.walletId == 4);
    CHECK(w1.currencyType == "USDT");
    CHECK(w1.balance == -12.5);
    CHECK(w1.nativeBalance == 300.125);
    CHECK(w1.bonusBalance == 0.012345);
    CHECK(w1.moneySpent == 777.75);
    CHECK(!w1.isOutsideWallet);
    CHECK(w1.notes == "n");
}

static void testAssetValue() {
    SECTION("AssetValue: cache hit, static fallback, unknown, mismatched load");
    // AssetValue (the core's no-internet placeholder price path, compiled
    // into the app) had no test coverage of its own.
    AssetValue av;
    CHECK(av.getPrice("ZZZTEST") == 0.0);     // cache miss + not in the table
    StaticPrices staticPrices;
    CHECK(av.getPrice("BTC") == staticPrices.prices.at("BTC"));   // static fallback
    CHECK(av.getPrice("BTC") > 0.0);
    av.loadCacheWithData({"BTC"}, {123.456});
    CHECK(av.getPrice("BTC") == 123.456);     // fresh cache wins over the table
    // Defensive pairing: only the overlapping prefix of mismatched vectors.
    av.loadCacheWithData({"AAA1", "AAA2", "AAA3"}, {1.5, 2.5});
    CHECK(av.getPrice("AAA1") == 1.5);
    CHECK(av.getPrice("AAA2") == 2.5);
    CHECK(av.getPrice("AAA3") == 0.0);
}

static void testPriceHierarchyReorder() {
    SECTION("AssetValue: stale (any age) beats the static baseline; getStale sentinel");
    // The reordered hierarchy: fresh (5-min TTL) -> stale (any age) ->
    // static baseline -> 0.0, plus the getStale accessor it leans on.
    PriceCache pc;
    CHECK_NEAR(pc.getStale("NEVER1"), -1.0, 1e-9);   // never cached: sentinel
    pc.addPrice("NEVER1", 42.0);
    CHECK_NEAR(pc.getStale("NEVER1"), 42.0, 1e-9);
    pc.addPrice("NEVER1", 0.0);                      // a stored "no value" answer
    CHECK_NEAR(pc.getStale("NEVER1"), 0.0, 1e-9);
    pc.ageEntryForTest("NEVER1", 301);               // just past the TTL
    CHECK_NEAR(pc.checkCache("NEVER1"), -1.0, 1e-9); // fresh read now misses
    CHECK_NEAR(pc.getStale("NEVER1"), 0.0, 1e-9);    // but the stale value survives

    AssetValue av;
    // TTL-expired entry -> the stale (real) price, NOT the 2023-era
    // static baseline (the non-triviality guards keep the assertions
    // from passing against an identical value).
    StaticPrices table;
    CHECK(table.prices.at("LTC") != 55.5);
    av.loadCacheWithData({"LTC"}, {55.5});
    CHECK_NEAR(av.getPrice("LTC"), 55.5, 1e-12);     // fresh
    av.ageCacheEntryForTest("LTC", 301);
    CHECK_NEAR(av.getPrice("LTC"), 55.5, 1e-12);     // stale wins over the table
    CHECK(av.getPrice("LTC") != table.prices.at("LTC"));

    // A stored 0.0 (the endpoint said "no value") counts as the stale
    // entry and beats the static table: no invented baseline.
    CHECK(table.prices.at("BCH") > 0.0);
    av.loadCacheWithData({"BCH"}, {0.0});
    av.ageCacheEntryForTest("BCH", 301);
    CHECK_NEAR(av.getPrice("BCH"), 0.0, 1e-12);

    // Never cached: known symbol -> the static baseline; unknown -> 0.0.
    CHECK_NEAR(av.getPrice("XRP"), table.prices.at("XRP"), 1e-12);
    CHECK(av.getPrice("XRP") > 0.0);
    CHECK_NEAR(av.getPrice("ZZQUNKNOWN"), 0.0, 1e-12);   // not in the table

    // Fresh entry wins over the still-present aged one (regression):
    // replacing an aged entry restores the fresh layer immediately.
    av.loadCacheWithData({"DOGE"}, {1.0});
    av.ageCacheEntryForTest("DOGE", 301);
    CHECK_NEAR(av.getPrice("DOGE"), 1.0, 1e-12);
    CHECK(table.prices.at("DOGE") != 2.0);
    av.loadCacheWithData({"DOGE"}, {2.0});
    CHECK_NEAR(av.getPrice("DOGE"), 2.0, 1e-12);     // fresh replacement wins
}

static void testParseBudgetWalletBuildAbort() {
    SECTION("wallet build honours the parse budget (clean abort, no throw)");
    // The parse-loop abort was covered; the wallet-BUILD re-check (the
    // "aborting wallet build" path) was not: an expiry between parse and
    // build must degrade to an empty, still-ready manager, not a throw.
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase"),
            cdcLine("2023-04-02 12:34:56", "ETH", "2", "200", "crypto_purchase"),
    };
    TransactionManager::setParseBudgetMs(0);
    TransactionParser parser(data);
    parser.parseFromCsv(Mode::CDC);
    CHECK(parser.getTransactions().size() == 2);
    TransactionManager::setParseBudgetMs(-1);    // expires before the build
    auto tm = std::make_unique<TransactionManager>();
    tm->setTransactions(parser.getTransactions(), Mode::CDC);
    tm->processTransactions();                   // must not throw
    CHECK(TransactionManager::parseBudgetExceeded());
    bool allEmpty = true;
    for (const auto &ws: tm->getWallets())
        if (!ws.second.getTransactions().empty()) allEmpty = false;
    CHECK(allEmpty);                             // nothing attached to any wallet
    CHECK_NEAR(tm->getTotalMoneySpent(), 0.0, 1e-12);
    CHECK(tm->isReady());                        // pipeline completed, just with no data
    // The cut reports the AGGREGATION stop: every row was already parsed,
    // so the unprocessed count stays 0 (that is what separates this
    // wording from a parser cut).
    CHECK(TransactionManager::wasLastParseCutByBudget());
    CHECK(TransactionManager::lastParseUnprocessedRows() == 0);
    // Same payload with a clean budget yields the full state (the abort
    // changed the outcome, not the data) - and the fresh parse unit
    // resets the result state again.
    TransactionManager::setParseBudgetMs(120000);
    auto tmOk = buildTmFromCdcLines(data);
    int attached = 0;
    for (const auto &ws: tmOk->getWallets())
        attached += static_cast<int>(ws.second.getTransactions().size());
    CHECK(attached == 2);
    CHECK(!TransactionManager::wasLastParseCutByBudget());
    CHECK(TransactionManager::lastParseUnprocessedRows() == 0);
}

static void testThreadLocalBudgetIsolation() {
    SECTION("parse budget is thread_local: an expired budget on one thread cannot starve another");
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase"),
            cdcLine("2023-04-02 12:34:56", "ETH", "2", "200", "crypto_purchase"),
    };
    std::atomic<bool> otherAborted{false};
    std::thread other([&]() {
        // This thread's deadline is expired; if the budget were process-
        // global, the main thread's parse below would abort as well.
        TransactionManager::setParseBudgetMs(-1);
        TransactionParser p(data);
        p.parseFromCsv(Mode::CDC);
        otherAborted = p.getTransactions().empty();
    });
    TransactionManager::setParseBudgetMs(0);     // main thread: no budget
    TransactionParser mainParser(data);
    mainParser.parseFromCsv(Mode::CDC);
    CHECK(mainParser.getTransactions().size() == 2);
    other.join();
    CHECK(otherAborted.load());
    TransactionManager::setParseBudgetMs(0);     // leave the main thread clean
}

static void testThreadStress() {
    SECTION("thread stress: parallel instances stay consistent, racing re-runs serialize");
    // The app runs every core op on the single "cpp-core" thread; this is
    // the further guarantee the core should keep: many independent managers
    // alive at once (per-manager state must not leak across instances)
    // and racing processTransactions() calls must serialize through the
    // manager mutex (no torn wallet-build writes, ASan-clean). Note
    // processTransactions() is single-use by design — re-runs append to the
    // ledgers again, so both sides run the SAME number of times.
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-04-01 12:34:56", "BTC", "1", "100", "crypto_purchase"),
            cdcLine("2023-04-02 12:34:56", "ETH", "2", "50", "crypto_purchase"),
            cdcLine("2023-04-03 12:34:56", "BTC", "-0.5", "40", "crypto_withdraw"),
    };
    auto ref = buildTmFromCdcLines(data);        // reference state, one build
    const size_t refWallets = ref->getWallets().size();
    const double refSpent = ref->getTotalMoneySpent();

    std::atomic<int> inconsistent{0};
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&inconsistent, &data, refWallets, refSpent]() {
            for (int i = 0; i < 25; ++i) {
                auto m = buildTmFromCdcLines(data);
                if (m->getWallets().size() != refWallets
                    || std::abs(m->getTotalMoneySpent() - refSpent) > 1e-9)
                    inconsistent++;
            }
        });
    }
    for (auto &th: threads) th.join();
    CHECK(inconsistent.load() == 0);

    // Sequential reference: FOUR processTransactions() runs (the app of
    // re-runs — deterministic under correct serialization).
    auto seq = std::make_unique<TransactionManager>();
    TransactionParser seqParser(data);
    seqParser.parseFromCsv(Mode::CDC);
    seq->setTransactions(seqParser.getTransactions(), Mode::CDC);
    for (int i = 0; i < 4; ++i) seq->processTransactions();
    const double seqSpent = seq->getTotalMoneySpent();

    std::atomic<bool> threw{false};
    auto racing = std::make_unique<TransactionManager>();
    TransactionParser raceParser(data);
    raceParser.parseFromCsv(Mode::CDC);
    racing->setTransactions(raceParser.getTransactions(), Mode::CDC);
    std::vector<std::thread> racers;
    racers.reserve(4);
    for (int t = 0; t < 4; ++t) {
        racers.emplace_back([&racing, &threw]() {
            try {
                racing->processTransactions();
            } catch (...) {
                threw = true;
            }
        });
    }
    for (auto &th: racers) th.join();
    CHECK(!threw.load());
    CHECK_NEAR(racing->getTotalMoneySpent(), seqSpent, 1e-9);
}

static void testCdcCryptoFixture() {
    SECTION("fixture: CDC crypto export");
    auto lines = loadFixture("cdc_crypto.csv");
    if (lines.empty()) return;
    TransactionParser parser(lines);
    parser.parseFromCsv(Mode::CDC);
    CHECK(parser.getTransactions().size() == 16);
    CHECK(parser.getFailedLines() == 1);
    auto tm = std::make_unique<TransactionManager>();
    tm->setTransactions(parser.getTransactions(), Mode::CDC);
    tm->processTransactions();
    const auto &w = tm->getWallets();
    CHECK(w.size() == 7);
    CHECK_NEAR(w.at("CRO").getBalance(), 768.41, 1e-9);
    // The CRO withdrawal's native (14.19) books on the outside wallet only.
    CHECK_NEAR(w.at("CRO").getMoneySpent(), 344.81, 1e-9); // 359.00 - 14.19
    CHECK_NEAR(w.at("CRO").getBonusBalance(), 105.01, 1e-9);
    CHECK_NEAR(w.at("BTC").getBalance(), 0.0004, 1e-12);
    CHECK_NEAR(w.at("BTC").getMoneySpent(), 12.87, 1e-9);
    CHECK_NEAR(w.at("ETH").getBalance(), 0.0005, 1e-12);
    CHECK_NEAR(w.at("ETH").getMoneySpent(), 0.25, 1e-9);
    CHECK_NEAR(w.at("LUNA").getBalance(), -0.01323, 1e-12);
    CHECK_NEAR(w.at("LUNA").getBonusBalance(), -0.01323, 1e-12);
    CHECK_NEAR(w.at("LUNC").getBalance(), 0.01323, 1e-12);
    CHECK_NEAR(w.at("LUNA2").getMoneySpent(), 0.027, 1e-9);
    CHECK_NEAR(w.at("EUR").getBalance(), -99.41, 1e-9);
    CHECK_NEAR(w.at("EUR").getMoneySpent(), 99.41, 1e-9);
    // card scope: inner wallets, the "EUR" wallet skipped
    double spent = 0.0, bonus = 0.0, bal = 0.0;
    for (const auto &wc: w)
        if (wc.first != "EUR") {
            spent += wc.second.getMoneySpent();
            bonus += wc.second.getBonusBalance();
            bal += wc.second.getBalance();
        }
    CHECK_NEAR(spent, 357.95738, 1e-9); // 372.14738 - 14.19 (withdrawal native)
    CHECK_NEAR(bonus, 105.0147, 1e-9);
    CHECK_NEAR(bal, 768.415, 1e-9);
    const auto &out = tm->getOutWallets();
    CHECK(out.size() == 1);                    // only the withdrawn asset leaves
    CHECK(out.count("CRO") == 1);
    CHECK_NEAR(out.at("CRO").getBalance(), 38.90, 1e-9);
    CHECK_NEAR(out.at("CRO").getMoneySpent(), -14.19, 1e-9);
    const auto &c = tm->getCurrencies();
    CHECK(c.size() == 7);
    CHECK(c[0] == "ETH" && c[1] == "BTC" && c[2] == "CRO" && c[3] == "EUR"
          && c[4] == "LUNA" && c[5] == "LUNC" && c[6] == "LUNA2");
    std::vector<double> prices(c.size(), 1.0);
    prices[1] = 50.0;                          // BTC
    tm->setPrices(prices);
    tm->calculateWalletBalances();
    double value = 0.0;
    for (const auto &wc: w)
        if (wc.first != "EUR") value += tm->getValueOfAssets(wc.second.getWalletId());
    CHECK_NEAR(value, 768.4346, 1e-9);
}

static void testCardFixture() {
    SECTION("fixture: CDC card export");
    auto lines = loadFixture("cdc_card.csv");
    if (lines.empty()) return;
    TransactionParser parser(lines);
    parser.parseFromCsv(Mode::Card);
    CHECK(parser.getTransactions().size() == 4);
    CHECK(parser.getFailedLines() == 0);
    const auto &txs = parser.getTransactions();
    CHECK(txs[0].getTransactionTypeString() == "Supermarket");   // card type is the description
    CHECK_NEAR(txs[0].getAmount(), 10.0, 1e-12);
    CHECK_NEAR(txs[0].getNativeAmount(), -10.0, 1e-12);
    TransactionManager tm;
    tm.setTransactions(parser.getTransactions(), Mode::Card);
    tm.processTransactions();
    auto state = tm.getTransactionManagerState();
    CHECK(state.hasCardTxData);
    CHECK(!state.hasTxData);
    const auto &cw = tm.getCardWallets();
    CHECK(cw.size() == 4);                      // one wallet per merchant
    CHECK(cw.count("Supermarket") == 1);
    CHECK(cw.count("Cafeteria") == 1);
    CHECK(cw.count("Streaming") == 1);
    CHECK(cw.count("Groceries") == 1);
    double spent = 0.0, bal = 0.0;
    for (const auto &wc: cw) {
        spent += wc.second.getMoneySpent();
        bal += wc.second.getBalance();
    }
    CHECK_NEAR(spent, -72.0, 1e-9);
    CHECK_NEAR(bal, 72.0, 1e-9);
}

// The Curve card path: 11-column fixture with per-field mapping, the
// REFUNDED/Declined exclusion (rows kept, totals + ledger flow skipped),
// and the full persistence round trip of the exclusion flag.
static void testCurveCard() {
    SECTION("fixture: Curve card export (REFUNDED/Declined exclusion)");
    auto lines = loadFixture("curve_card.csv");
    if (lines.empty()) return;
    TransactionParser parser(lines);
    parser.parseFromCsv(Mode::CurveCard);
    const auto &txs = parser.getTransactions();
    CHECK(parser.getFailedLines() == 1);   // 10-field line without trailing comma
    CHECK(txs.size() == 7);   // 8 data lines, one broken skipped

    // Line 0 - plain EUR purchase, full date+time form
    {
        const auto &d = txs[0].getTransactionData();
        CHECK(d.transactionDate.tm_year == 2026 - 1900);
        CHECK(d.transactionDate.tm_mon == 2);
        CHECK(d.transactionDate.tm_mday == 1);
        CHECK(d.transactionDate.tm_hour == 9);
        CHECK(d.transactionDate.tm_min == 30);
        CHECK(d.description == "Supermarket");
        CHECK(txs[0].getTransactionTypeString() == "Supermarket");   // merchant routes the wallet
        CHECK(d.currencyType == "EUR");
        CHECK_NEAR(txs[0].getAmount(), 42.50, 1e-12);
        CHECK_NEAR(txs[0].getNativeAmount(), 42.50, 1e-12);
        CHECK(txs[0].getToCurrencyType() == "EUR");
        CHECK(d.transactionHash == "Curve Blue1111");
        CHECK(d.notes == "Groceries: Basket");
        CHECK(txs[0].getTransactionType() == STRING);
        CHECK(!txs[0].isExcludedFromTotals());
    }
    // Line 1 - foreign spend: the native amount comes from the foreign column
    CHECK_NEAR(txs[1].getAmount(), 125.00, 1e-12);
    CHECK_NEAR(txs[1].getNativeAmount(), 118.00, 1e-12);
    CHECK(txs[1].getToCurrencyType() == "USD");

    // Line 2 - date-only (midnight) + empty foreign amount falls back to the
    // funding amount; the trailing-comma Notes split is padded back
    {
        const auto &d = txs[2].getTransactionData();
        CHECK(d.transactionDate.tm_hour == 0);
        CHECK(d.transactionDate.tm_min == 0);
        CHECK(d.transactionDate.tm_sec == 0);
        CHECK_NEAR(txs[2].getNativeAmount(), 15.25, 1e-12);
        CHECK(d.notes == "Groceries: ");   // unconditional "Category: " concat
    }

    // Lines 3/4 - the exclusion set; lines 5/6 stay included
    CHECK(txs[3].isExcludedFromTotals());
    CHECK(txs[4].isExcludedFromTotals());
    CHECK(!txs[5].isExcludedFromTotals());
    CHECK(!txs[6].isExcludedFromTotals());
    // Line 5 - thousands-separator amounts; line 6 - quoted merchant with a comma
    CHECK_NEAR(txs[5].getAmount(), 1234.56, 1e-12);
    CHECK(txs[6].getTransactionData().description == "Burger King, Downtown");

    // Mode dispatch: the card bucket only
    TransactionManager tm;
    tm.setTransactions(parser.getTransactions(), Mode::CurveCard);
    tm.processTransactions();
    auto state = tm.getTransactionManagerState();
    CHECK(state.hasCardTxData);
    CHECK(!state.hasTxData);
    CHECK(tm.getActiveModes() == 2);

    // One merchant wallet per Type; the empty "EUR -> EUR" special wallet is
    // pruned by the empty-wallet cleanup (no row of this fixture routes to it)
    const auto &cw = tm.getCardWallets();
    CHECK(cw.size() == 6);
    double spent = 0.0, bal = 0.0;
    for (const auto &wc: cw) {
        spent += wc.second.getMoneySpent();
        bal += wc.second.getBalance();
    }
    // The REFUNDED (42.50) and Declined (10.00) rows are excluded:
    // spent = 42.50 + 118.00 + 15.25 + 1234.56 + 25.00
    // bal   = that + the 125.00 funding amount of the foreign-spend row
    CHECK_NEAR(spent, 1435.31, 1e-9);
    CHECK_NEAR(bal, 1442.31, 1e-9);

    // Excluded rows: present in the card list AND in the merchant wallet's
    // list, but they apply no totals and no daily-series ledger flow
    {
        CHECK(tm.getCardTransactions().size() == 7);
        const auto &sw = cw.at("Supermarket");
        CHECK(sw.getTransactions().size() == 2);
        CHECK_NEAR(sw.getBalance(), 42.50, 1e-9);
        CHECK_NEAR(sw.getMoneySpent(), 42.50, 1e-9);
        const auto &ledger = sw.getLedger();
        CHECK(ledger.size() == 2);
        CHECK_NEAR(ledger[1].balance, 0.0, 1e-12);
        CHECK_NEAR(ledger[1].spent, 0.0, 1e-12);
        CHECK_NEAR(ledger[1].bonus, 0.0, 1e-12);
        const auto &cl = cw.at("Club Entry");
        CHECK(cl.getTransactions().size() == 1);
        CHECK_NEAR(cl.getBalance(), 0.0, 1e-9);
        CHECK_NEAR(cl.getMoneySpent(), 0.0, 1e-9);
    }

    // The XML export carries the flag and reads it back
    {
        const std::string refundedXml = txs[3].getTransactionData().serializeToXml();
        CHECK(refundedXml.find("<excludedFromTotals>true</excludedFromTotals>") !=
              std::string::npos);
        const std::string normalXml = txs[0].getTransactionData().serializeToXml();
        CHECK(normalXml.find("<excludedFromTotals>false</excludedFromTotals>") !=
              std::string::npos);
        TransactionData fromRefunded;
        fromRefunded.deserializeFromXml(refundedXml);
        CHECK(fromRefunded.excludedFromTotals);
        TransactionData fromNormal;
        fromNormal.deserializeFromXml(normalXml);
        CHECK(!fromNormal.excludedFromTotals);
    }

    // v3 save/load round trip: rows, flag and totals survive
    {
        const std::string dir = (std::filesystem::temp_directory_path() /
                                 "nf_tx_core_curve_tests").string() + "/";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        CHECK(tm.saveData(dir));
        CHECK(tm.checkSavedData(dir));

        TransactionManager loaded;
        loaded.loadData(dir);
        CHECK(loaded.getCardTransactions().size() == 7);
        int excluded = 0;
        for (const auto &tx: loaded.getCardTransactions())
            if (tx.isExcludedFromTotals()) excluded++;
        CHECK(excluded == 2);
        double spentLoaded = 0.0, balLoaded = 0.0;
        for (const auto &wc: loaded.getCardWallets()) {
            spentLoaded += wc.second.getMoneySpent();
            balLoaded += wc.second.getBalance();
        }
        CHECK_NEAR(spentLoaded, 1435.31, 1e-9);
        CHECK_NEAR(balLoaded, 1442.31, 1e-9);
        CHECK(loaded.getCardWallets().at("Supermarket").getTransactions().size() == 2);
        std::filesystem::remove_all(dir);
    }
}

static void testKrakenFixture() {
    SECTION("fixture: Kraken trade CSV");
    auto lines = loadFixture("kraken_trades.csv");
    if (lines.empty()) return;
    TransactionParser parser(lines);
    parser.parseFromCsv(Mode::Kraken);
    CHECK(parser.getTransactions().size() == 2);
    CHECK(parser.getFailedLines() == 1);
    const auto &txs = parser.getTransactions();
    CHECK(txs[0].getCurrencyType() == "BTC");           // buy keeps the base coin
    CHECK(txs[0].getTransactionTypeString() == "buy");
    CHECK_NEAR(txs[0].getAmount(), 0.0007, 1e-15);
    CHECK_NEAR(txs[0].getNativeAmount(), 49.5, 1e-12);
    CHECK_NEAR(txs[0].getFeeAmount(), 0.15, 1e-12);
    CHECK(txs[1].getCurrencyType() == "ZEUR");          // sell takes the quote part
    CHECK(txs[1].getTransactionTypeString() == "sell");
    CHECK_NEAR(txs[1].getAmount(), -0.0003, 1e-15);
    CHECK_NEAR(txs[1].getNativeAmount(), -21.0, 1e-12);
    TransactionManager tm;
    tm.setTransactions(parser.getTransactions(), Mode::Kraken);
    tm.processTransactions();
    const auto &w = tm.getWallets();
    CHECK(w.size() == 2);                       // BTC (buy) + ZEUR (sell row)
    CHECK_NEAR(w.at("BTC").getBalance(), 0.0007, 1e-15);
    CHECK_NEAR(w.at("BTC").getMoneySpent(), 49.5, 1e-12);
}

static void testBlockPitFixture() {
    SECTION("fixture: BlockPit export (CRLF)");
    auto lines = loadFixture("blockpit_export.csv");
    if (lines.empty()) return;
    CHECK(lines[0].back() == '\r');                 // real exports are CRLF
    TransactionParser parser(lines);
    parser.parseFromCsv(Mode::BlockPit);
    CHECK(parser.getTransactions().size() == 14);
    CHECK(parser.getFailedLines() == 1);
    auto tm = buildTmFromBlockPitLines(lines);
    const auto &w = tm->getWallets();
    CHECK(w.size() == 9);
    CHECK_NEAR(w.at("KFEE").getBalance(), 1.49, 1e-9);    // 30 gift - 28.51 fee
    CHECK_NEAR(w.at("SCX").getBalance(), 0.5, 1e-12);
    CHECK_NEAR(w.at("SCX").getBonusBalance(), 0.5, 1e-12);
    CHECK_NEAR(w.at("ETH").getBalance(), 0.0005, 1e-12);  // Staking: balance, no bonus
    CHECK_NEAR(w.at("BTC").getBalance(), 0.00321752, 1e-12);
    CHECK_NEAR(w.at("BTC").getMoneySpent(), 150.8298, 1e-9);
    CHECK_NEAR(w.at("EUR").getBalance(), -120.8298, 1e-9);
    CHECK_NEAR(w.at("Laptop").getBalance(), -4.95974, 1e-12);   // swap debit
    CHECK_NEAR(w.at("BCH").getBalance(), 0.0729602, 1e-12);
    CHECK_NEAR(w.at("CRO").getBalance(), 1.25, 1e-9);
    CHECK_NEAR(w.at("CRO").getBonusBalance(), 2.5, 1e-9);
    CHECK_NEAR(w.at("USDT").getBalance(), 49.0, 1e-9);        // 50 airdrop - 1 fee
    CHECK_NEAR(w.at("USDT").getBonusBalance(), 49.0, 1e-9);
    double spent = 0.0, bonus = 0.0;
    for (const auto &wc: w) {
        spent += wc.second.getMoneySpent();
        bonus += wc.second.getBonusBalance();
    }
    CHECK_NEAR(spent, 150.8298, 1e-9);
    CHECK_NEAR(bonus, 52.0, 1e-9);
    const auto &out = tm->getOutWallets();
    CHECK(out.size() == 1);                       // only BCH left the inner wallets
    CHECK_NEAR(out.at("BCH").getBalance(), -0.05, 1e-12);
}

// Import/restore state contract on the C surface — the exact sequence the
// Firebase restore runs (init -> clearAll -> set*Data). Pins, at the API
// level the app binds, that the set*Data functions are pure appenders
// (a second sequence onto a live core accumulates), that clearAll() is
// the only reset (every container empty, both active-mode flags down,
// hence getActiveModes()==0), that a set*Data sequence AFTER clearAll is
// a REPLACE (no residue from the cleared state), and the load-bearing
// order: clearAll() goes through DataHolder::GetTransactionManager(),
// which throws without a TM, so it is only safe AFTER init() has created
// the TM (a reversed order would carry the exception across the C
// boundary = terminate). Runs before testDlsymSurfaceLinkage on purpose:
// it starts from the process's cold (never initialized) DataHolder.
static void testImportStateSemantics() {
    SECTION("import state: init/clearAll/set*Data contract on the C surface");

    // Cold core: init creates the TM even when there is no saved data to
    // load (returns false then), and clearAll() must be safe immediately
    // after it (no-op on an empty TM, no throw across the C boundary).
    CHECK(!init("tests.log", ""));
    clearAll();
    CHECK(getActiveModes() == 0);
    CHECK(getWalletsAsStrings().empty());
    CHECK(getTransactionsAsStrings().empty());

    // State A: one crypto wallet + one tx, one card wallet + one card tx.
    setWalletData({walletDataXml(1, "BTC", 2.5)});
    setTransactionData({txDataXml(10, "BTC", 2.5)});
    setCardWalletData({walletDataXml(100, "CRO", 72.0)});
    setCardTransactionData({txDataXml(20, "EUR", -72.0)});
    CHECK(getActiveModes() == 3);
    CHECK(getWalletsAsStrings().size() == 1);
    CHECK(getWalletsAsStrings()[0].find("<currencyType>BTC</currencyType>")
          != std::string::npos);
    CHECK(getTransactionsAsStrings().size() == 1);
    CHECK(getCardWalletsAsStrings().size() == 1);
    CHECK(getCardTransactionsAsStrings().size() == 1);

    // APPEND — the as-is contract: a set*Data sequence WITHOUT clearAll
    // accumulates onto the live state (new wallet keys add, transactions
    // always append, the monotonic active-mode flags stay on).
    setWalletData({walletDataXml(2, "ETH", 1.0)});
    setTransactionData({txDataXml(11, "ETH", 1.0)});
    setCardWalletData({walletDataXml(101, "VISA", 10.0)});
    setCardTransactionData({txDataXml(21, "EUR", -10.0)});
    CHECK(getActiveModes() == 3);
    CHECK(getWalletsAsStrings().size() == 2);
    CHECK(getWalletsAsStrings()[0].find("<currencyType>BTC</currencyType>")
          != std::string::npos);
    CHECK(getWalletsAsStrings()[1].find("<currencyType>ETH</currencyType>")
          != std::string::npos);
    CHECK(getTransactionsAsStrings().size() == 2);
    CHECK(getCardWalletsAsStrings().size() == 2);
    CHECK(getCardTransactionsAsStrings().size() == 2);

    // The reset: clearAll() empties every container and drops both
    // active-mode flags (getActiveModes counts exactly those two).
    clearAll();
    CHECK(getActiveModes() == 0);
    CHECK(getWalletsAsStrings().empty());
    CHECK(getTransactionsAsStrings().empty());
    CHECK(getCardWalletsAsStrings().empty());
    CHECK(getCardTransactionsAsStrings().empty());

    // REPLACE: a set*Data sequence after clearAll rebuilds the state from
    // its own entries alone — no BTC/ETH (A) or VISA (B) residue.
    setWalletData({walletDataXml(3, "USDC", 40.0)});
    setTransactionData({txDataXml(30, "USDC", 40.0)});
    setCardWalletData({walletDataXml(102, "MC", 5.0)});
    setCardTransactionData({txDataXml(31, "EUR", -5.0)});
    CHECK(getActiveModes() == 3);
    CHECK(getWalletsAsStrings().size() == 1);
    CHECK(getWalletsAsStrings()[0].find("<currencyType>USDC</currencyType>")
          != std::string::npos);
    CHECK(getWalletsAsStrings()[0].find("BTC") == std::string::npos);
    CHECK(getTransactionsAsStrings().size() == 1);
    CHECK(getTransactionsAsStrings()[0].find("<transactionId>30</transactionId>")
          != std::string::npos);
    CHECK(getCardWalletsAsStrings().size() == 1);
    CHECK(getCardWalletsAsStrings()[0].find("<currencyType>MC</currencyType>")
          != std::string::npos);
    CHECK(getCardTransactionsAsStrings().size() == 1);

    // The net-worth snapshot on the C surface: the core injects its own
    // current EUR wallet value (the internal EUR buffer INCLUDED - there
    // is none in this state, USDC only) into the sheet's snapshot; the
    // caller sees the line only. A future liability start keeps the
    // balance the full principal, determinate on the wall clock.
    {
        calculateBalances();   // builds the currency order
        const auto &curs = getCurrencies();
        std::vector<double> prices(curs.size(), 0.0);
        for (size_t i = 0; i < curs.size(); i++) {
            if (curs[i] == "USDC") prices[i] = 1.0;
        }
        setPrice(prices);   // 40 USDC @ 1.0 = 40
        CHECK(sheetSet("L;cs-1;Future;2500;0;2035-01-01;-;-"));
        CHECK(sheetSet("A;cs-2;Car;VEHICLE;7000;2026-01-01T12:00:00;-"));
        const std::vector<std::string> snap = sheetSnapshot();
        CHECK(snap.size() == 1);
        CHECK(snap[0] == "NW;40;7000;2500;4540");
        CHECK(sheetDelete("Lcs-1"));
        CHECK(sheetDelete("Acs-2"));   // leave the holder state clean
    }
}

// The dlsym surface — the C API the CLI tester loads via dlopen. Pins the
// C linkage of the three series accessors: taking the addresses below only
// LINKS if the definitions in library.cpp (compiled into this target)
// carry C linkage (the extern "C" declarations in library.h). A regression
// to C++ linkage / mangled exports fails this target's link with an
// undefined plain symbol — the same failure the tester's plain-name dlsym
// would hit at runtime.
static void testDlsymSurfaceLinkage() {
    SECTION("dlsym surface: series accessors export plain C names");
    using DailyFn = std::vector<std::string> (*)();
    using SubDailyFn = std::vector<std::string> (*)(int, long long);
    using ReturnsFn = std::string (*)(const std::string &);
    const DailyFn daily = getDailyWalletSeries;
    const SubDailyFn spent = getSubDailySpentSeries;
    const SubDailyFn wallet = getSubDailyWalletSeries;
    const ReturnsFn returns = computeReturns;
    CHECK(daily != nullptr);
    CHECK(spent != nullptr);
    CHECK(wallet != nullptr);
    // Link-level plain-name check: a C++-linkage (mangled) regression in
    // the computeReturns definition fails this target's link.
    CHECK(returns != nullptr);

    // Runtime confirmation through the plain symbols, consuming them the
    // way the tester does (C init -> calculate -> read the series).
    std::vector<std::string> data{
            CDC_HEADER,
            cdcLine("2023-05-10 08:00:00", "BTC", "2.5", "100", "crypto_purchase"),
    };
    CHECK(initWithData(data, Mode::CDC, "tests.log", 0));
    calculateBalances();
    CHECK((daily() == std::vector<std::string>({
            "BTC;2023-05-10;2.500000;0.000000"})));
    CHECK((spent(6 * 3600, 0) == std::vector<std::string>({
            "2023-05-10 06:00;100.000000"})));
    CHECK((wallet(6 * 3600, 0) == std::vector<std::string>({
            "BTC;2023-05-10 06:00;2.500000;0.000000"})));
    // Degenerate window: empty, no crash (contract shared with the TM level).
    CHECK(spent(0, 0).empty());
}

// ------------------------------------------------- multi-file append tests ---
//
// The combined-view append contract on the real C surface (DataHolder):
// file 1 through initWithData (REPLACE), the rest through appendWithData
// with exact-tuple dedup at append time. The independent tally is a fresh
// manager built from the union's rows in ONE import (identical per-row
// wallet assignment, different ingestion path) - the two must agree on
// every wallet figure.

// Synthetic files (CDC crypto): A and B with disjoint tuples, C with one
// unparsable line. Distinct dates/descriptions keep every tuple unique
// within and across A/B; C's good row does not collide with either.
static const std::vector<std::string> APPEND_FILE_A = {
        CDC_HEADER,
        cdcLine("2023-05-01 10:00:00", "BTC", "1", "100", "crypto_purchase", "buy1"),
        cdcLine("2023-05-02 10:00:00", "ETH", "2", "200", "crypto_purchase", "buy2"),
};
static const std::vector<std::string> APPEND_FILE_B = {
        CDC_HEADER,
        cdcLine("2023-05-03 11:00:00", "BTC", "3", "300", "crypto_purchase", "buy3"),
        cdcLine("2023-05-04 11:00:00", "ETH", "4", "400", "crypto_purchase", "buy4"),
};
static const std::vector<std::string> APPEND_FILE_C = {
        CDC_HEADER,
        cdcLine("2023-05-05 09:00:00", "LUNA", "7", "70", "crypto_purchase", "buy6"),
        "only,four,columns,here",   // counts as this file's own failed line
};

//! A fresh manager with exactly the union of [first] + [rest...] rows (the
//! rest header-less, like a single CSV with concatenated body rows).
static std::unique_ptr<TransactionManager> buildReferenceUnion(
        const std::vector<std::string> &first,
        std::initializer_list<const std::vector<std::string> *> rest,
        Mode mode) {
    std::vector<std::string> lines = first;
    for (const auto *f: rest)
        lines.insert(lines.end(), f->begin() + 1, f->end());
    TransactionParser parser(lines);
    parser.parseFromCsv(mode);
    CHECK(parser.getFailedLines() == 0);
    auto tm = std::make_unique<TransactionManager>();
    tm->setTransactions(parser.getTransactions(), mode);
    tm->processTransactions();
    return tm;
}

static void checkWalletMapsEqual(const std::map<std::string, Wallet> &live,
                                 const std::map<std::string, Wallet> &reference) {
    CHECK(live.size() == reference.size());
    for (const auto &[cur, w]: live) {
        auto it = reference.find(cur);
        CHECK(it != reference.end());
        if (it == reference.end()) continue;
        const Wallet &r = it->second;
        CHECK_NEAR((double) w.getBalance(), (double) r.getBalance(), 1e-6);
        CHECK_NEAR((double) w.getMoneySpent(), (double) r.getMoneySpent(), 1e-6);
        CHECK_NEAR((double) w.getBonusBalance(), (double) r.getBonusBalance(), 1e-6);
        CHECK(w.getTransactions().size() == r.getTransactions().size());
    }
}

//! The live (DataHolder) manager's append counters - the JNI-level view
//! of what the Kotlin side accumulates along the sequence.
static size_t duplicateSkipsNow() {
    return DataHolder::GetInstance().GetTransactionManager()->getDuplicateSkips();
}

static size_t failedLinesNow() {
    return DataHolder::GetInstance().GetTransactionManager()->getFailedLines();
}

static double referenceCardSpent() {
    auto curve = loadFixture("curve_card.csv");
    TransactionParser parser(curve);
    parser.parseFromCsv(Mode::CurveCard);
    auto tm = std::make_unique<TransactionManager>();
    tm->setTransactions(parser.getTransactions(), Mode::CurveCard);
    tm->processTransactions();
    return tm->getTotalMoneySpentCard();
}

static void testMultiFileAppend() {
    SECTION("multi-file append: union, exact-tuple dedup, mixed modes, budget, 10k overlap");
    const std::string log = "tests.log";
    const int cdc = static_cast<int>(Mode::CDC);
    const int curve = static_cast<int>(Mode::CurveCard);

    // 1. UNION: init(A) + append(B) == one import of A+B (independent tally)
    CHECK(initWithData(APPEND_FILE_A, cdc, log, 0));
    CHECK(getActiveModes() == 1);
    CHECK(getTransactionsAsStrings().size() == 2);
    CHECK(appendWithData(APPEND_FILE_B, cdc, log, 0));
    CHECK(getTransactionsAsStrings().size() == 4);
    CHECK(duplicateSkipsNow() == 0);
    {
        auto ref = buildReferenceUnion(APPEND_FILE_A, {&APPEND_FILE_B}, Mode::CDC);
        CHECK(ref->getTransactions().size() == 4);
        checkWalletMapsEqual(DataHolder::GetInstance().GetTransactionManager()->getWallets(),
                             ref->getWallets());
        CHECK_NEAR(getTotalMoneySpent(), ref->getTotalMoneySpent(), 1e-9);
        CHECK_NEAR(getTotalBonus(), ref->getTotalBonus(), 1e-9);
    }

    // 2. EXACT duplicates: re-appending A skips both rows, counted; the
    // aggregates stay at the union figures (no double account).
    CHECK(appendWithData(APPEND_FILE_A, cdc, log, 0));
    CHECK(duplicateSkipsNow() == 2);
    CHECK(getTransactionsAsStrings().size() == 4);
    CHECK(appendWithData(APPEND_FILE_B, cdc, log, 0));
    CHECK(duplicateSkipsNow() == 4);
    CHECK(getTransactionsAsStrings().size() == 4);
    {
        auto ref = buildReferenceUnion(APPEND_FILE_A, {&APPEND_FILE_B}, Mode::CDC);
        checkWalletMapsEqual(DataHolder::GetInstance().GetTransactionManager()->getWallets(),
                             ref->getWallets());
        CHECK_NEAR(getTotalMoneySpent(), ref->getTotalMoneySpent(), 1e-9);
    }

    // 2b. A file's OWN failed-line count rides the append (the Kotlin side
    // accumulates it per file); re-appending the same file skips only its
    // good row and re-counts the failed line.
    CHECK(appendWithData(APPEND_FILE_C, cdc, log, 0));
    CHECK(failedLinesNow() == 1);
    CHECK(getTransactionsAsStrings().size() == 5);
    CHECK(appendWithData(APPEND_FILE_C, cdc, log, 0));
    CHECK(duplicateSkipsNow() == 5);
    CHECK(failedLinesNow() == 1);
    CHECK(getTransactionsAsStrings().size() == 5);

    // 3. MIXED, card first: init(curve) + append(A) -> both buckets live,
    // getActiveModes()==3, each side identical to its own single import.
    auto curveFile = loadFixture("curve_card.csv");
    const double refCardSpent = referenceCardSpent();
    CHECK(initWithData(curveFile, curve, log, 0));
    CHECK(getActiveModes() == 2);
    CHECK(failedLinesNow() == 1);   // the curve fixture's broken line (its own count)
    CHECK(getCardTransactionsAsStrings().size() == 7);
    CHECK(appendWithData(APPEND_FILE_A, cdc, log, 0));
    CHECK(getActiveModes() == 3);
    CHECK(getTransactionsAsStrings().size() == 2);
    CHECK(getCardTransactionsAsStrings().size() == 7);
    CHECK(duplicateSkipsNow() == 0);
    CHECK_NEAR(getTotalMoneySpentCard(), refCardSpent, 1e-9);
    {
        auto refA = buildTmFromCdcLines(APPEND_FILE_A);
        checkWalletMapsEqual(DataHolder::GetInstance().GetTransactionManager()->getWallets(),
                             refA->getWallets());
        CHECK_NEAR(getTotalMoneySpent(), refA->getTotalMoneySpent(), 1e-9);
    }

    // 4. MIXED, crypto first: init(A) + append(curve) -> same union
    // regardless of the order the buckets are filled in.
    CHECK(initWithData(APPEND_FILE_A, cdc, log, 0));
    CHECK(getActiveModes() == 1);
    CHECK(appendWithData(curveFile, curve, log, 0));
    CHECK(getActiveModes() == 3);
    CHECK(getTransactionsAsStrings().size() == 2);
    CHECK(getCardTransactionsAsStrings().size() == 7);
    CHECK(failedLinesNow() == 1);   // the appended file's own count
    CHECK(duplicateSkipsNow() == 0);
    CHECK_NEAR(getTotalMoneySpentCard(), refCardSpent, 1e-9);
    {
        auto refA = buildTmFromCdcLines(APPEND_FILE_A);
        checkWalletMapsEqual(DataHolder::GetInstance().GetTransactionManager()->getWallets(),
                             refA->getWallets());
    }

    // 5. 10k OVERLAP: the first 1000 rows init, then the FULL file appends -
    // the 1000 shared rows are counted duplicates and the union is exactly
    // the full file imported alone (same wallet figures, independent tally).
    const auto full10k = loadFixture("blockpit_10k.csv");
    const size_t shared = 1000;
    CHECK(full10k.size() > shared + 1);
    {
        // reference: the full file alone (stack TM, independent of DataHolder)
        TransactionParser refParser(full10k);
        refParser.parseFromCsv(Mode::CDC);
        CHECK(refParser.getFailedLines() == 0);
        if (refParser.getFailedLines() != 0 || refParser.getTransactions().empty()) {
            CHECK(false);   // fixture shape mismatch - fail clean, don't throw
            return;
        }
        auto ref = std::make_unique<TransactionManager>();
        ref->setTransactions(refParser.getTransactions(), Mode::CDC);
        ref->processTransactions();
        CHECK(ref->getTransactions().size() == 10000);

        std::vector<std::string> head(full10k.begin(), full10k.begin() + shared + 1);
        CHECK(initWithData(head, cdc, log, 0));
        CHECK(getTransactionsAsStrings().size() == shared);
        CHECK(appendWithData(full10k, cdc, log, 0));
        CHECK(duplicateSkipsNow() == shared);
        CHECK(getTransactionsAsStrings().size() == 10000);
        checkWalletMapsEqual(DataHolder::GetInstance().GetTransactionManager()->getWallets(),
                             ref->getWallets());
        CHECK_NEAR(getTotalMoneySpent(), ref->getTotalMoneySpent(), 1e-6);
    }

    // 6. BUDGET: the parse budget applies to appends too (051 discipline).
    CHECK(initWithData(APPEND_FILE_A, cdc, log, 0));
    // 6a. already-expired budget: the parse aborts before taking any row -
    // a clean false, the live model untouched.
    CHECK(!appendWithData(APPEND_FILE_B, cdc, log, -1));
    CHECK(getTransactionsAsStrings().size() == 2);
    CHECK(duplicateSkipsNow() == 0);
    // 6b. an expiry between parse and wallet build degrades to the row
    // landing in the bucket WITHOUT being attached to a wallet (no throw,
    // no double account) - the append sibling of the init abort path.
    {
        TransactionManager::setParseBudgetMs(0);
        TransactionParser parserB(APPEND_FILE_B);
        parserB.parseFromCsv(Mode::CDC);
        CHECK(parserB.getTransactions().size() == 2);
        const long double spentBefore =
                DataHolder::GetInstance().GetTransactionManager()->getTotalMoneySpent();
        TransactionManager::setParseBudgetMs(-1);   // expires before the build
        DataHolder::GetInstance().GetTransactionManager()
                ->appendTransactions(parserB.getTransactions(), Mode::CDC);
        TransactionManager::setParseBudgetMs(0);
        CHECK(getTransactionsAsStrings().size() == 4);   // rows landed in the bucket
        CHECK_NEAR(DataHolder::GetInstance().GetTransactionManager()->getTotalMoneySpent(),
                   spentBefore, 1e-12);   // but nothing was attached to a wallet
        CHECK(DataHolder::GetInstance().GetTransactionManager()->isReady());
    }

    // Leave the C surface on a settled, fully-processed state.
    CHECK(initWithData(APPEND_FILE_A, cdc, log, 0));
    CHECK(duplicateSkipsNow() == 0);
}

// ------------------------------------------------------------------ main ---

static void testCsvExport() {
    SECTION("csv export: the byte-frozen contract (header, rows, sheet, stamp)");
    {
        // The full fixture state: rows in ALL THREE wallet maps (the
        // withdrawal lands in the inner AND the outside wallet, a
        // same-name/timestamp tie), an escaped ';' in both free-text
        // columns, a midnight timestamp, an empty description, and one
        // sheet entry per collection.
        auto tm = std::make_unique<TransactionManager>();
        std::vector<std::string> cryptoLines{
                CDC_HEADER,
                cdcLine("2026-03-01 00:00:00", "BTC", "-0.5", "-500", "crypto_withdrawal",
                        "Exchange out"),
                cdcLine("2026-03-02 09:00:00", "BTC", "0.5", "500", "crypto_purchase",
                        "Coinbase; buy"),
                cdcLine("2026-03-05 08:00:00", "ETH", "1", "3000",
                        "crypto_earn_interest_paid", ""),
                // crypto_purchase on the internal EUR buffer: the row joins
                // the wallet (a supercharger_deposit would not - the export
                // is the per-wallet state, exactly what the core shows).
                cdcLine("2026-03-03 14:15:00", "EUR", "500", "500", "crypto_purchase",
                        "Salary in"),
        };
        TransactionParser cryptoParser(cryptoLines);
        cryptoParser.parseFromCsv(Mode::CDC);
        tm->setTransactions(cryptoParser.getTransactions(), Mode::CDC);

        // Card: no header (the parser skips it only when it matches).
        TransactionParser cardParser({
                "2026-03-04 16:20:00,Grocery Store Pay; loyalty,VISA,10.50,,,,-10.50,0,",
                "2026-03-01 11:11:11,EUR -> EUR,EUR,25,,,,25,0,",
        });
        cardParser.parseFromCsv(Mode::Card);
        tm->setTransactions(cardParser.getTransactions(), Mode::Card);
        tm->processTransactions();

        BalanceSheet sheet;
        CHECK(sheet.setLine("L;fx-1;Mortgage;100000;4.8;2025-01-01;-;800"));
        CHECK(sheet.setLine("A;fx-2;House;REAL_ESTATE;350000;2025-06-15T14:05:09;-"));
        CHECK(sheet.setLine("G;fx-3;Target;1000;-;NET_WORTH"));

        // The Pinned instant: the liability trailer and the export stamp
        // share it, so the WHOLE file is byte-deterministic (the hand
        // value: n=15 months, i=0.004, P=100000, M=800 ->
        // 100000*1.004^15 - 800*(1.004^15-1)/0.004 = 93829.052746... ->
        // 93829.05275 at 10 significant digits).
        std::tm now{};
        now.tm_year = 2026 - 1900;
        now.tm_mon = 3;
        now.tm_mday = 1;
        now.tm_hour = 12;
        now.tm_min = 0;
        now.tm_sec = 0;

        CHECK(std::string(CsvExport::kHeaderLine) ==
              "date;time;wallet;wallet_kind;tx_kind;amount;currency;symbol;description");

        const std::string file = CsvExport::buildCsvFile(*tm, sheet, now);

        // The WHOLE file, byte-exact. Row order = (wallet name, date,
        // time); the inner/outside tie of the withdrawal keeps the
        // collection order (crypto inner before outside); a midnight
        // timestamp renders an empty time; the sheet section rides the
        // line serde (L with the core-computed trailer); the stamp is
        // the pinned instant.
        const std::string expected =
              "date;time;wallet;wallet_kind;tx_kind;amount;currency;symbol;description\n"
              "2026-03-01;;BTC;crypto;crypto_withdrawal;-0.5;BTC;BTC;Exchange out\n"
              "2026-03-01;;BTC;outside;crypto_withdrawal;-0.5;BTC;BTC;Exchange out\n"
              "2026-03-02;09:00;BTC;crypto;crypto_purchase;0.5;BTC;BTC;Coinbase;; buy\n"
              "2026-03-05;08:00;ETH;crypto;crypto_earn_interest_paid;1;ETH;ETH;\n"
              "2026-03-03;14:15;EUR;crypto;crypto_purchase;500;EUR;;Salary in\n"
              "2026-03-01;11:11;EUR -> EUR;card;EUR -> EUR;25;EUR;;EUR -> EUR\n"
              // The card wallet name is the payment text itself (the wallet
              // map key), so it - and the tx_kind - carry the escaped ';';
              // the card's "currency" column holds the CARD name (VISA),
              // which is not fiat, so the symbol column repeats it.
              "2026-03-04;16:20;Grocery Store Pay;; loyalty;card;Grocery Store Pay;; loyalty;10.5;VISA;VISA;Grocery Store Pay;; loyalty\n"
              "# balance sheet\n"
              "L;fx-1;Mortgage;100000;4.8;2025-01-01;-;800;bal;93829.05275\n"
              "A;fx-2;House;REAL_ESTATE;350000;2025-06-15T14:05:09;-\n"
              "G;fx-3;Target;1000;-;NET_WORTH\n"
              "# exported 2026-04-01T12:00:00 by cdcsvparser\n";
        CHECK(file == expected);
    }

    SECTION("csv export: empty state is header + marker + stamp only");
    {
        TransactionManager tm;
        BalanceSheet sheet;
        std::tm now{};
        now.tm_year = 2027 - 1900;
        now.tm_mon = 11;
        now.tm_mday = 31;
        now.tm_hour = 23;
        now.tm_min = 59;
        now.tm_sec = 59;
        CHECK(CsvExport::buildCsvFile(tm, sheet, now) ==
              "date;time;wallet;wallet_kind;tx_kind;amount;currency;symbol;description\n"
              "# balance sheet\n"
              "# exported 2027-12-31T23:59:59 by cdcsvparser\n");
    }

    SECTION("csv export: the C surface rides the live holder state");
    {
        auto &dh = DataHolder::GetInstance();
        // Sanitize the holder's sheet (the earlier tests leave it in
        // their own states): every entry of every active kind goes.
        for (const std::string &kind: {"L", "A", "G"}) {
            for (const auto &line: dh.sheetList(kind)) {
                const size_t semi = line.find(';');
                CHECK(semi != std::string::npos);
                if (semi == std::string::npos) continue;
                CHECK(dh.sheetDelete(kind + line.substr(1, semi - 1)));
            }
            CHECK(dh.sheetList(kind).empty());
        }

        // A live but never-processed manager: no rows, an empty sheet ->
        // exactly the three skeleton lines (the stamp value is the wall
        // clock, so the structure - not the value - is pinned here).
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
        const std::string file = exportCsv();   // the plain C surface
        std::vector<std::string> lines;
        {
            size_t start = 0;
            while (true) {
                const size_t end = file.find('\n', start);
                if (end == std::string::npos) break;
                lines.push_back(file.substr(start, end - start));
                start = end + 1;
            }
            CHECK(file.substr(file.size() - 1) == "\n");   // trailing newline
        }
        CHECK(lines.size() == 3);
        CHECK(lines[0] ==
              "date;time;wallet;wallet_kind;tx_kind;amount;currency;symbol;description");
        CHECK(lines[1] == "# balance sheet");
        CHECK(lines[2].size() == 45);   // "# exported " + ISO-8601 (19) + " by cdcsvparser"
        CHECK(lines[2].rfind("# exported ", 0) == 0);
        CHECK(lines[2].ends_with(" by cdcsvparser"));

        // Leave the C surface on a never-ready manager again.
        dh.SetTransactionManager(std::make_unique<TransactionManager>());
    }
}


// --------------------------------------------------- return-metric tests ---
//
// The TWR + MWR/IRR contract of the core's pure return metric, pinned
// against hand-computed fixtures (every expected figure derived
// independently of the code under test): the chain-of-periods TWR, the
// bisection MWR (the window-rate equation V_end = V_0*x + the grown
// post-start flows), the 365-geometric annualization, and the honesty
// markers (<30-day span, all-zero CF stream, non-computable periods).

static void testReturnMetrics() {
    SECTION("portfolio-return metric: hand-computed TWR + MWR/IRR fixtures");

    // Two-deposit drift: +10 % price, a 50 EUR deposit mid-way, +10 % on
    // both blocks. Hand: TWR = 1.1 * 1.0 * 1.1 - 1 = 0.21 exactly; the
    // MWR root of 176 - 100x - 50*sqrt(x) = 0 is x = 1.21 exactly (the
    // quadratic in y = sqrt(x): 50y^2 - 25y - 88 = 0), so both metrics
    // agree here by construction.
    CHECK((computeReturns(
                  "R;2023-01-01;100;0\n"
                  "R;2023-02-10;110;0\n"
                  "R;2023-02-20;160;50\n"
                  "R;2023-04-11;176;0") == "TWR;0.2100;1.0052;3;0\nMWR;0.2100;1.0052;100"));

    // Mid-hold withdrawal: +15 % price, a 15 EUR withdrawal, then a
    // smaller drift. Hand: TWR = 1.15 * 1.0 * (224.0601/215) - 1
    // = 0.1985 (four decimals); the MWR equation 224.0601 - 200x +
    // 15*x^(1/3) = 0 was built to root at x ~= 1.2 (r ~= 0.2), which a
    // withdrawal must pull ABOVE the TWR - the metrics diverge and each
    // pins its own figure.
    CHECK((computeReturns(
                  "R;2023-01-01;200;0\n"
                  "R;2023-01-31;230;0\n"
                  "R;2023-03-02;215;-15\n"
                  "R;2023-04-01;224.0601;0") == "TWR;0.1985;1.0838;3;0\nMWR;0.2000;1.0947;90"));

    // Zero-CF drift: the all-zero flow stream -> the MWR line carries the
    // marker (the IRR carries nothing beyond the TWR), while the TWR
    // degrades to the price return: 54/50 - 1 = 0.08 exactly.
    CHECK((computeReturns(
                  "R;2023-01-01;50;0\n"
                  "R;2023-02-10;60;0\n"
                  "R;2023-04-01;54;0") == "TWR;0.0800;0.3663;2;0\nMWR;INSUFFICIENT;90"));

    // Single deposit at inception (the deposit rides the inception
    // value; V_0 already values it, so the post-start CF stream is
    // empty): the MWR reduces to x = V_end/V_0 and MUST equal the TWR
    // (121/100 - 1 = 0.21, the convergence the metric must show).
    CHECK((computeReturns(
                  "R;2023-01-01;100;100\n"
                  "R;2023-04-11;121;0") == "TWR;0.2100;1.0052;1;0\nMWR;0.2100;1.0052;100"));

    // Two-year annualization: +20 % per 365 days over exactly 730 days.
    // Hand: absolute = 1.44 - 1 = 0.44, annualized = sqrt(1.44) - 1
    // = 0.20 EXACTLY (the 365-geometric over 730 days is a square
    // root); the all-zero CF stream keeps the MWR marked.
    CHECK((computeReturns(
                  "R;2022-01-01;100;0\n"
                  "R;2023-01-01;120;0\n"
                  "R;2024-01-01;144;0") == "TWR;0.4400;0.2000;2;0\nMWR;INSUFFICIENT;730"));

    // Negative-V skip window: two periods start at V <= 0 and are
    // skipped (the count is reported, not hidden); the computable period
    // pins the absolute (45-30)/30 = 0.5, the MWR roots the equation
    // 45 + 5x - 37*x^(50/80) = 0 (V_0 < 0 - still well defined in the
    // bracket).
    CHECK((computeReturns(
                  "R;2023-01-01;-5;0\n"
                  "R;2023-01-11;-2;0\n"
                  "R;2023-01-31;30;37\n"
                  "R;2023-03-22;45;0") == "TWR;0.5000;5.3594;1;2\nMWR;0.8427;15.2593;80"));

    // Sub-30-day window: neither metric may emit a percentage (no
    // silent, no unqualified figures) - both lines carry the marker,
    // the counts stay honest (two computable periods, 20-day span).
    CHECK((computeReturns(
                  "R;2023-01-01;100;0\n"
                  "R;2023-01-11;110;10\n"
                  "R;2023-01-21;130;0") == "TWR;INSUFFICIENT;2;0\nMWR;INSUFFICIENT;20"));

    // Malformed lines (foreign prefix, impossible calendar day 2023-02-
    // 30, a short line, a non-numeric value) are skipped, not fatal: the
    // metric runs over the three valid lines (span 96, TWR = 1.3 - 1
    // = 0.30 exactly).
    CHECK((computeReturns(
                  "R;2023-01-01;100;0\n"
                  "not-a-metrics-line\n"
                  "R;2023-02-30;105;0\n"
                  "R;2023-01-05\n"
                  "R;2023-01-06;abc;0\n"
                  "R;2023-01-07;110;10\n"
                  "R;2023-04-07;143;0") == "TWR;0.3000;1.7116;2;0\nMWR;0.3019;1.7270;96"));

    // Empty input has no honest number: the result is empty (the caller
    // shows its own not-ready state), not a marker pair.
    CHECK(computeReturns("").empty());

    // One line is a zero-day window: the metrics are INSUFFICIENT with
    // the honest zero counts (no period, no span).
    CHECK((computeReturns("R;2023-01-01;100;0") ==
            "TWR;INSUFFICIENT;0;0\nMWR;INSUFFICIENT;0"));
}

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
    testTxIdCounterRoundTrip();
    testBinaryUtil();
    testUnlimitedPersistence();
    testLegacyV2Migration();
    testSaveFormatV3LegacyRead();
    testBalanceSheet();
    testNetWorthSnapshot();
    testPriceCache();
    testPriceHierarchyReorder();
    testXmlSerialization();
    testFileLogLevels();
    testMoneySpentSeries();
    testDailySeries();
    testSubDailySeries();
    testSubDailyOpeningLine();
    testDailyWalletSeries();
    testDailyWalletSeriesEdges();
    testDailyWalletSeriesEurScope();
    testSubDailyWalletSeriesEurScope();
    testParseBudget();
    testXmlRoundTrip();
    testAssetValue();
    testParseBudgetWalletBuildAbort();
    testThreadLocalBudgetIsolation();
    testThreadStress();
    testCdcCryptoFixture();
    testCardFixture();
    testCurveCard();
    testKrakenFixture();
    testBlockPitFixture();
    testImportStateSemantics();
    testMultiFileAppend();
    testCsvExport();
    testDlsymSurfaceLinkage();
    testAlerts();
    testReturnMetrics();

    std::printf("\n%d checks, %d failure%s\n", g_checks, g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
