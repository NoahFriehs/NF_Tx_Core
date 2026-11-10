
#include <vector>
#include <stdexcept>
#include "TransactionParser.h"
#include "TransactionManager.h"
#include "FileLog.h"

TransactionParser::TransactionParser() = default;

TransactionParser::~TransactionParser() = default;

TransactionParser::TransactionParser(const std::vector<std::string> &data) {
    if (data.empty()) throw std::invalid_argument("Data is empty");
    this->data = data;
    hasData = true;
}

namespace {
// Windows exports end lines with \r and UTF-8 files may start with a BOM;
// both silently break exact header comparison and pollute the last field.
std::string cleanCsvLine(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    static const std::string bom = "\xEF\xBB\xBF";
    if (line.rfind(bom, 0) == 0) line.erase(0, bom.size());
    return line;
}
}  // namespace

void TransactionParser::parseFromCsv(Mode mode) {

    if (!hasData) throw std::invalid_argument("Data is empty");

    switch (mode) {

        case CDC: {
            parseCDC();
            break;
        }
        case Card:
            parseCard();
            break;
        case CurveCard:
            parseCurveCard();
            break;
        case Kraken:
            parseKraken();
            break;
        case BlockPit:
            parseBlockPit();
            break;
        case Custom:
            throw std::invalid_argument("Custom mode not implemented");
        case Default:
            parseCDC();
            break;
    }

}

std::vector<BaseTransaction> &TransactionParser::getTransactions() {
    return transactions;
}

void TransactionParser::parseCDC() {

    if ("Timestamp (UTC),Transaction Description,Currency,Amount,To Currency,To Amount,Native Currency,Native Amount,Native Amount (in USD),Transaction Kind,Transaction Hash" ==
        cleanCsvLine(data[0]))
        data.erase(data.begin());   // remove header row, if needed

    for (size_t row = 0; row < data.size(); ++row) {
        const auto &item = data[row];
        if (TransactionManager::parseBudgetExceeded()) {
            FileLog::e("TransactionParser", "Parse budget exceeded: stopping early");
            // The cut row itself was never parsed either.
            TransactionManager::markLastParseCut(static_cast<int>(data.size() - row));
            break;
        }
        BaseTransaction transaction;
        try {
            transaction.parseCDC(cleanCsvLine(item));
            transactions.push_back(transaction);
        } catch (const std::exception &e) {
            // Skip the broken line, continue with the rest (same behavior
            // as the Kotlin core's amountTxFailed counter).
            failedLines++;
            FileLog::w("TransactionParser",
                       "Skipping unparsable line: " + std::string(e.what()));
        }
    }

    FileLog::i("TransactionParser",
               "Parsed " + std::to_string(transactions.size()) + " transactions" +
                       (failedLines > 0
                            ? ", " + std::to_string(failedLines) + " line(s) failed"
                            : ""));

    data.clear();
    hasData = false;
}

void TransactionParser::parseCard() {

    if ("Timestamp (UTC),Transaction Description,Currency,Amount,To Currency,To Amount,Native Currency,Native Amount,Native Amount (in USD),Transaction Kind,Transaction Hash" ==
        cleanCsvLine(data[0]))
        data.erase(data.begin());   // remove header row, if needed

    for (size_t row = 0; row < data.size(); ++row) {
        const auto &item = data[row];
        if (TransactionManager::parseBudgetExceeded()) {
            FileLog::e("TransactionParser", "Parse budget exceeded: stopping early");
            // The cut row itself was never parsed either.
            TransactionManager::markLastParseCut(static_cast<int>(data.size() - row));
            break;
        }
        BaseTransaction transaction;
        try {
            transaction.parseCard(cleanCsvLine(item));
            transactions.push_back(transaction);
        } catch (const std::exception &e) {
            // Skip the broken line, continue with the rest (same behavior
            // as the Kotlin core's amountTxFailed counter).
            failedLines++;
            FileLog::w("TransactionParser",
                       "Skipping unparsable line: " + std::string(e.what()));
        }
    }

    FileLog::i("TransactionParser",
               "Parsed " + std::to_string(transactions.size()) + " transactions" +
                       (failedLines > 0
                            ? ", " + std::to_string(failedLines) + " line(s) failed"
                            : ""));

    data.clear();
    hasData = false;

}

void TransactionParser::parseCurveCard() {
    // The exact Curve export header (kept in sync with the Kotlin
    // curveTxString literal in AppTypeIdentifier).
    constexpr const char *CURVE_HEADER =
            "Date (YYYY-MM-DD as UTC),Merchant,Txn Amount (Funding Card),"
            "Txn Currency (Funding Card),Txn Amount (Foreign Spend),"
            "Txn Currency (Foreign Spend),Card Name,Card Last 4 Digits,Type,"
            "Category,Notes";
    if (CURVE_HEADER == cleanCsvLine(data[0]))
        data.erase(data.begin());   // remove header row, if needed

    for (size_t row = 0; row < data.size(); ++row) {
        const auto &item = data[row];
        if (TransactionManager::parseBudgetExceeded()) {
            FileLog::e("TransactionParser", "Parse budget exceeded: stopping early");
            // The cut row itself was never parsed either.
            TransactionManager::markLastParseCut(static_cast<int>(data.size() - row));
            break;
        }
        BaseTransaction transaction;
        try {
            transaction.parseCurve(cleanCsvLine(item));
            transactions.push_back(transaction);
        } catch (const std::exception &e) {
            // Skip the broken line, continue with the rest (same behavior
            // as the Kotlin core's amountTxFailed counter).
            failedLines++;
            FileLog::w("TransactionParser",
                       "Skipping unparsable line: " + std::string(e.what()));
        }
    }

    FileLog::i("TransactionParser",
               "Parsed " + std::to_string(transactions.size()) + " transactions" +
                       (failedLines > 0
                            ? ", " + std::to_string(failedLines) + " line(s) failed"
                            : ""));

    data.clear();
    hasData = false;
}

void TransactionParser::parseKraken() {

    if (R"raw("txid","ordertxid","pair","time","type","ordertype","price","cost","fee","vol","margin","misc","ledgers")raw" ==
        cleanCsvLine(data[0]))
        data.erase(data.begin());   // remove header row, if needed

    for (size_t row = 0; row < data.size(); ++row) {
        const auto &item = data[row];
        if (TransactionManager::parseBudgetExceeded()) {
            FileLog::e("TransactionParser", "Parse budget exceeded: stopping early");
            // The cut row itself was never parsed either.
            TransactionManager::markLastParseCut(static_cast<int>(data.size() - row));
            break;
        }
        BaseTransaction transaction;
        try {
            transaction.parseKraken(cleanCsvLine(item));
            transactions.push_back(transaction);
        } catch (const std::exception &e) {
            // Skip the broken line, continue with the rest (same behavior
            // as the Kotlin core's amountTxFailed counter).
            failedLines++;
            FileLog::w("TransactionParser",
                       "Skipping unparsable line: " + std::string(e.what()));
        }
    }

    FileLog::i("TransactionParser",
               "Parsed " + std::to_string(transactions.size()) + " transactions" +
                       (failedLines > 0
                            ? ", " + std::to_string(failedLines) + " line(s) failed"
                            : ""));

    data.clear();
    hasData = false;
}

void TransactionParser::parseBlockPit() {
    // BlockPit header detection
    constexpr const char *BLOCKPIT_HEADER = "Date (UTC);Integration Name;Label;Outgoing Asset;Outgoing Amount;"
            "Incoming Asset;Incoming Amount;Fee Asset (optional);Fee Amount (optional);"
            "Comment (optional);Trx. ID (optional);Source Type;Source Name";
    if (BLOCKPIT_HEADER == cleanCsvLine(data[0]) ||
        cleanCsvLine(data[0]).find("Date (UTC)") != std::string::npos) {
        data.erase(data.begin());   // remove header row, if needed
    }

    for (size_t row = 0; row < data.size(); ++row) {
        const auto &item = data[row];
        if (TransactionManager::parseBudgetExceeded()) {
            FileLog::e("TransactionParser", "Parse budget exceeded: stopping early");
            // The cut row itself was never parsed either.
            TransactionManager::markLastParseCut(static_cast<int>(data.size() - row));
            break;
        }
        BaseTransaction transaction;
        try {
            transaction.parseBlockPit(cleanCsvLine(item));
            transactions.push_back(transaction);
        } catch (const std::exception &e) {
            // Skip the broken line, continue with the rest (same behavior
            // as the Kotlin core's amountTxFailed counter).
            failedLines++;
            FileLog::w("TransactionParser",
                       "Skipping unparsable line: " + std::string(e.what()));
        }
    }

    FileLog::i("TransactionParser",
               "Parsed " + std::to_string(transactions.size()) + " transactions" +
                       (failedLines > 0
                            ? ", " + std::to_string(failedLines) + " line(s) failed"
                            : ""));

    data.clear();
    hasData = false;
}
