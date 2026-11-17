// See BalanceSheet.h for the contract.

#include "BalanceSheet.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "FileLog.h"

// ----------------------------------------------------------------------------
// serde helpers
// ----------------------------------------------------------------------------

namespace SheetSerde {

std::string escapeField(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c: s) {
        if (c == ';') out += ';';
        out += c;
    }
    return out;
}

std::vector<std::string> splitLine(const std::string &line) {
    std::vector<std::string> fields;
    std::string current;
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == ';') {
            if (i + 1 < line.size() && line[i + 1] == ';') {
                current += ';';   // the escape: a literal ';' inside the field
                i++;
                continue;
            }
            fields.push_back(std::move(current));
            current.clear();
        } else {
            current += line[i];
        }
    }
    fields.push_back(std::move(current));
    return fields;
}

bool parseDecimal(const std::string &s, double &out) {
    if (s.empty()) return false;
    std::size_t pos = 0;
    try {
        out = std::stod(s, &pos);
    } catch (...) {
        return false;
    }
    if (pos != s.size() || !std::isfinite(out)) return false;
    return true;
}

namespace {

int daysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    int d = days[month - 1];
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) d = 29;
    return d;
}

} // namespace

bool parseDate(const std::string &s, int &year, int &month, int &day) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i: {0, 1, 2, 3, 5, 6, 8, 9}) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    year = std::atoi(s.substr(0, 4).c_str());
    month = std::atoi(s.substr(5, 2).c_str());
    day = std::atoi(s.substr(8, 2).c_str());
    if (year < 1000 || year > 9999) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > daysInMonth(year, month)) return false;
    return true;
}

std::string formatDate(int year, int month, int day) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    return std::string(buf);
}

std::string formatDecimal(double v) {
    if (!std::isfinite(v) || v < 0.0) v = 0.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return std::string(buf);
}

std::tm todayWallClock() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

} // namespace SheetSerde

// ----------------------------------------------------------------------------
// Liability
// ----------------------------------------------------------------------------

double Liability::balanceAsOf(const std::tm &asOf) const {
    auto monthsIndex = [](int year, int month) { return year * 12 + (month - 1); };
    const long long nElapsed =
            std::max(0LL, static_cast<long long>(monthsIndex(asOf.tm_year + 1900, asOf.tm_mon + 1) -
                                                 monthsIndex(startYear, startMonth)));
    long long n = nElapsed;
    if (hasEndDate) {
        const long long nToEnd =
                std::max(0LL, static_cast<long long>(monthsIndex(endYear, endMonth) -
                                                     monthsIndex(startYear, startMonth)));
        // Month past the end date: the debt is settled by construction.
        if (nElapsed > nToEnd) return 0.0;
        n = std::min(nElapsed, nToEnd);
    }
    const double i = annualRatePct / 1200.0;
    double b;
    if (i <= 0.0) {
        b = monthlyPaymentEur > 0.0 ? principalEur - monthlyPaymentEur * static_cast<double>(n)
                                    : principalEur;
    } else {
        const double f = std::pow(1.0 + i, static_cast<double>(n));
        b = monthlyPaymentEur > 0.0
                   ? principalEur * f - monthlyPaymentEur * (f - 1.0) / i
                   : principalEur * f;
    }
    // An overpay settles the debt: the balance caps at zero, never negative.
    return b > 0.0 ? b : 0.0;
}

// ----------------------------------------------------------------------------
// BalanceSheet
// ----------------------------------------------------------------------------

namespace {

constexpr size_t kMaxIdLength = 64;
constexpr size_t kMaxLabelLength = 512;

bool validId(const std::string &id) {
    if (id.empty() || id.size() > kMaxIdLength) return false;
    for (char c: id) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
    }
    return true;
}

bool validLabel(const std::string &label) {
    if (label.empty()) return false;
    return label.size() <= kMaxLabelLength;
}

} // namespace

bool BalanceSheet::parseLine(const std::string &line, std::string &outId, Liability &out) {
    const std::vector<std::string> f = SheetSerde::splitLine(line);
    // Set form: kind + 7 fields; list form: + the "bal" marker + the value.
    if (f.size() != 8 && f.size() != 10) return false;
    const bool hasBalTrailer = f.size() == 10;
    if (f[0] != "L") return false;
    outId = f[1];
    if (!validId(outId)) return false;
    const std::string &label = f[2];
    if (!validLabel(label)) return false;

    double principal = 0.0;
    double rate = 0.0;
    double payment = 0.0;
    if (!SheetSerde::parseDecimal(f[3], principal) || principal < 0.0) return false;
    if (!SheetSerde::parseDecimal(f[4], rate) || rate < 0.0) return false;
    int sy = 0, sm = 0, sd = 0;
    if (!SheetSerde::parseDate(f[5], sy, sm, sd)) return false;
    const std::string &endField = f[6];
    const std::string &paymentField = f[7];
    if (paymentField != "-" && !SheetSerde::parseDecimal(paymentField, payment)) return false;
    if (payment < 0.0) return false;

    out = Liability();
    out.id = outId;
    out.label = label;
    out.principalEur = principal;
    out.annualRatePct = rate;
    out.startYear = sy;
    out.startMonth = sm;
    out.startDay = sd;
    out.monthlyPaymentEur = paymentField == "-" ? 0.0 : payment;
    if (endField != "-") {
        int ey = 0, em = 0, ed = 0;
        if (!SheetSerde::parseDate(endField, ey, em, ed)) return false;
        if (ey * 10000 + em * 100 + ed < sy * 10000 + sm * 100 + sd) return false;
        out.hasEndDate = true;
        out.endYear = ey;
        out.endMonth = em;
        out.endDay = ed;
    }
    if (hasBalTrailer && f[8] != "bal") return false;
    return true;
}

bool BalanceSheet::setLine(const std::string &line) {
    std::string id;
    Liability liability;
    if (!parseLine(line, id, liability)) {
        FileLog::w("BalanceSheet", "setLine rejected a line (shape/fields failed validation)");
        return false;
    }
    auto it = std::find_if(liabilities_.begin(), liabilities_.end(),
                           [&](const Liability &l) { return l.id == id; });
    if (it != liabilities_.end()) {
        *it = std::move(liability);   // upsert: replace in place, keep the position
    } else {
        liabilities_.push_back(std::move(liability));
    }
    return true;
}

bool BalanceSheet::deleteEntry(const std::string &kindId) {
    if (kindId.empty() || kindId[0] != 'L') return false;
    const std::string id = kindId.substr(1);
    auto it = std::find_if(liabilities_.begin(), liabilities_.end(),
                           [&](const Liability &l) { return l.id == id; });
    if (it == liabilities_.end()) return false;
    liabilities_.erase(it);
    return true;
}

std::string BalanceSheet::liabilityLine(const Liability &l, const std::tm &asOf) {
    std::string line = "L;";
    line += l.id;
    line += ';';
    line += SheetSerde::escapeField(l.label);
    line += ';';
    line += SheetSerde::formatDecimal(l.principalEur);
    line += ';';
    line += SheetSerde::formatDecimal(l.annualRatePct);
    line += ';';
    line += SheetSerde::formatDate(l.startYear, l.startMonth, l.startDay);
    line += ';';
    line += l.hasEndDate
            ? SheetSerde::formatDate(l.endYear, l.endMonth, l.endDay)
            : std::string("-");
    line += ';';
    line += l.monthlyPaymentEur > 0.0
            ? SheetSerde::formatDecimal(l.monthlyPaymentEur)
            : std::string("-");
    return line;
}

std::string BalanceSheet::lineOf(const Liability &l, const std::tm &asOf) {
    std::string line = liabilityLine(l, asOf);
    line += ";bal;";
    line += SheetSerde::formatDecimal(l.balanceAsOf(asOf));
    return line;
}

std::string BalanceSheet::lineOf(const Liability &l) {
    return liabilityLine(l, SheetSerde::todayWallClock());
}

std::vector<std::string> BalanceSheet::listLines(const std::string &kind, const std::tm &asOf) const {
    std::vector<std::string> lines;
    if (kind != "L") return lines;   // other collections resolve empty until active
    lines.reserve(liabilities_.size());
    for (const auto &l: liabilities_) lines.push_back(lineOf(l, asOf));
    return lines;
}

std::string BalanceSheet::serialize(const std::tm &asOf) const {
    std::string text;
    // All collections, one line each, in the stored order (later phases
    // append their entries here - the section shape never changes).
    for (const auto &l: liabilities_) {
        if (!text.empty()) text += '\n';
        text += lineOf(l, asOf);
    }
    return text;
}

size_t BalanceSheet::parse(const std::string &text) {
    size_t rejected = 0;
    std::vector<Liability> next;
    if (!text.empty()) {
        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t nl = text.find('\n', start);
            std::string line = text.substr(
                    start, nl == std::string::npos ? std::string::npos : nl - start);
            start = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
            if (line.empty()) continue;
            std::string id;
            Liability liability;
            if (parseLine(line, id, liability)) {
                next.push_back(std::move(liability));
            } else {
                ++rejected;
                FileLog::e("BalanceSheet",
                           "Sheet section line rejected on load (skipped): " + line);
            }
        }
    }
    liabilities_ = std::move(next);
    return rejected;
}
