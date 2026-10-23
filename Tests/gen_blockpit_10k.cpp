// Deterministic generator for blockpit_10k.csv (the 10k-row parse
// fixture). Pure formulas - no randomness - so the committed fixture bytes
// are fully reproducible:
//
//   NF_Tx_Core_GenBlockPit10K > Tests/fixtures/blockpit_10k.csv
//
// Row layout (CDC mode, the app's primary path): 10,000 data rows after the
// header. Row i (0-based):
//   ts       = 2023-01-01T00:00:00Z + i * 137 s  (~60 rows/day over 17
//             days; the 137 s step keeps every row on a distinct minute)
//   currency = BTC / ETH / LUNA / CRO           (i % 4)
//   kind     = i % 5:
//             0,1  crypto_purchase             +0.5 of the currency,
//                                                          native +50*(i%3+1)
//             2    crypto_withdrawal            -0.25 of the currency,
//                                                          native -25
//             3    crypto_earn_interest_paid    +0.1 of the currency (balance
//                                                          AND bonus),
//                                                          native +10
//             4    viban_purchase               -30 EUR in, +0.3 of the
//                                                          currency out (raw
//                                                          row: the CDC
//                                                          helpers cannot
//                                                          fill the To
//                                                          columns)
//
// Ground truth: big_fixture_check.cpp carries the verified per-currency
// totals (lattice i%4 x i%5 over 10000 rows; the committed numbers there
// were cross-checked against the parser output).
#include <cstdio>
#include <cstdlib>

// Howard Hinnant's civil_from_days: z = days since 1970-01-01.
static void civilFromDays(int z, int &y, int &m, int &d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
}

int main(int argc, char **argv) {
    const int rows = (argc > 1) ? atoi(argv[1]) : 10000;
    static const char *CUR[4] = {"BTC", "ETH", "LUNA", "CRO"};
    printf("Timestamp (UTC),Transaction Description,Currency,Amount,To Currency,"
           "To Amount,Native Currency,Native Amount,Native Amount (in USD),"
           "Transaction Kind,Transaction Hash\n");
    for (int i = 0; i < rows; ++i) {
        const int secs = i * 137;
        int y, m, d;
        civilFromDays(19358 + secs / 86400, y, m, d); // 2023-01-01 = day 19358
        const int h = (secs % 86400) / 3600;
        const int mi = (secs % 3600) / 60;
        const int s = secs % 60;
        const char *cur = CUR[i % 4];
        const int k = i % 5;
        char line[256];
        if (k <= 1) {
            const long native = 50L * (i % 3 + 1);
            snprintf(line, sizeof(line),
                     "%04d-%02d-%02d %02d:%02d:%02d,exchange %s,%s,0.5,,,USD,%ld,%ld,"
                     "crypto_purchase,",
                     y, m, d, h, mi, s, cur, cur, native, native);
        } else if (k == 2) {
            snprintf(line, sizeof(line),
                     "%04d-%02d-%02d %02d:%02d:%02d,exchange %s,%s,-0.25,,,USD,-25,-25,"
                     "crypto_withdrawal,",
                     y, m, d, h, mi, s, cur, cur);
        } else if (k == 3) {
            snprintf(line, sizeof(line),
                     "%04d-%02d-%02d %02d:%02d:%02d,earn %s,%s,0.1,,,USD,10,10,"
                     "crypto_earn_interest_paid,",
                     y, m, d, h, mi, s, cur, cur);
        } else {
            snprintf(line, sizeof(line),
                     "%04d-%02d-%02d %02d:%02d:%02d,savings %s,EUR,-30,%s,0.3,EUR,-30,"
                     "-33,viban_purchase,",
                     y, m, d, h, mi, s, cur, cur);
        }
        printf("%s\n", line);
    }
    return 0;
}
