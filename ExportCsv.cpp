#include "ExportCsv.h"
#include "BalanceSheet.h"
#include "Transaction/BaseTransaction.h"
#include "Structs.h"
#include "TransactionManager.h"
#include "Wallet/Wallet.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

namespace {

//! One export row: the sort key (wallet name + folded timestamp) plus
//! the already-built 9-column line.
struct CsvRow {
    std::string walletName;
    long long timeKey = 0;
    std::string line;
};

//! Fold a timestamp into one sortable long (ymdhms, zero-padded fields).
long long foldTime(const std::tm &t) {
    long long key = static_cast<long long>(t.tm_year + 1900);
    key = key * 100 + (t.tm_mon + 1);
    key = key * 100 + t.tm_mday;
    key = key * 100 + t.tm_hour;
    key = key * 100 + t.tm_min;
    key = key * 100 + t.tm_sec;
    return key;
}

//! Append one row per transaction of [wallets] (key = the wallet name,
//! the map's sorted order is the contract's stable tie-break) under the
//! kind token [kind] (the core's own wallet partition).
void appendWalletRows(const std::map<std::string, Wallet> &wallets, const char *kind,
                      std::vector<CsvRow> &out) {
    for (const auto &entry: wallets) {
        const std::string &name = entry.first;
        for (const BaseTransaction &tx: entry.second.getTransactions()) {
            // The getter copy carries the private fields (date,
            // description) that have no per-field accessor. The type
            // STRING comes from the live row: the TransactionData
            // projection only carries the enum ordinal.
            const TransactionData d = tx.getTransactionData();
            const std::string txKind = tx.getTransactionTypeString();

            char dateBuf[16] = {};
            std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &d.transactionDate);

            // Exactly-midnight is the core's "no time held" marker (the
            // date-only export forms parse to midnight): it renders
            // EMPTY, a held minute renders HH:MM.
            std::string timeBuf;
            if (d.transactionDate.tm_hour != 0 || d.transactionDate.tm_min != 0) {
                char tBuf[8] = {};
                std::strftime(tBuf, sizeof(tBuf), "%H:%M", &d.transactionDate);
                timeBuf = tBuf;
            }

            // The symbol column is the token code itself; fiat rows carry
            // none (the core's own fiat table decides).
            const std::string symbol =
                    BaseTransaction::isFiatCurrency(d.currencyType) ? std::string()
                                                                    : d.currencyType;

            CsvRow row;
            row.walletName = name;
            row.timeKey = foldTime(d.transactionDate);
            row.line = std::string(dateBuf) + ";" + timeBuf + ";" +
                       SheetSerde::escapeField(name) + ";" + kind + ";" +
                       SheetSerde::escapeField(txKind) + ";" +
                       SheetSerde::formatDecimalSigned(static_cast<double>(d.amount)) + ";" +
                       d.currencyType + ";" + symbol + ";" +
                       SheetSerde::escapeField(d.description);
            out.push_back(std::move(row));
        }
    }
}

} // namespace

namespace CsvExport {

const char *kHeaderLine =
        "date;time;wallet;wallet_kind;tx_kind;amount;currency;symbol;description";
const char *kBalanceSheetMarker = "# balance sheet";

std::string buildCsvFile(TransactionManager &tm, const BalanceSheet &sheet,
                         const std::tm &now) {
    std::vector<CsvRow> rows;
    // The collection order below is the stable tie-break of the sort:
    // the crypto inner wallets, then the outside wallets, then the card
    // wallets (identical name+date rows keep this order).
    appendWalletRows(tm.getWallets(), "crypto", rows);
    appendWalletRows(tm.getOutWallets(), "outside", rows);
    appendWalletRows(tm.getCardWallets(), "card", rows);

    std::stable_sort(rows.begin(), rows.end(),
                     [](const CsvRow &a, const CsvRow &b) {
                         if (a.walletName != b.walletName)
                             return a.walletName < b.walletName;
                         return a.timeKey < b.timeKey;
                     });

    const std::string section = sheet.serialize(now);

    std::string out;
    out.reserve(64 + 96 * rows.size() + section.size());
    out += kHeaderLine;
    out += '\n';
    for (const CsvRow &row: rows) {
        out += row.line;
        out += '\n';
    }
    out += kBalanceSheetMarker;
    out += '\n';
    if (!section.empty()) {
        out += section;
        out += '\n';
    }
    const std::string stamp = SheetSerde::formatTimestamp(
            now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min,
            now.tm_sec);
    out += "# exported " + stamp + " by cdcsvparser";
    out += '\n';
    return out;
}

} // namespace CsvExport
