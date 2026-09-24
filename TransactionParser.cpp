
#include <vector>
#include <stdexcept>
#include "TransactionParser.h"
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
        case Kraken:
            parseKraken();
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

    for (const auto &item: data) {
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

    for (const auto &item: data) {
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

void TransactionParser::parseKraken() {

    if (R"raw("txid","ordertxid","pair","time","type","ordertype","price","cost","fee","vol","margin","misc","ledgers")raw" ==
        cleanCsvLine(data[0]))
        data.erase(data.begin());   // remove header row, if needed

    for (const auto &item: data) {
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
