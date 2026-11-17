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

std::string formatDecimalSigned(double v) {
    if (!std::isfinite(v)) v = 0.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return std::string(buf);
}

bool parseTimestamp(const std::string &s, int &year, int &month, int &day,
                    int &hour, int &minute, int &second) {
    if (s.size() != 19 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
        s[13] != ':' || s[16] != ':')
        return false;
    int y, m, d;
    if (!parseDate(s.substr(0, 10), y, m, d)) return false;
    // The lambda result is a BOOL (not the value: "00" is a valid part).
    auto part = [](const std::string &t, int &out) -> bool {
        if (t[0] < '0' || t[0] > '9' || t[1] < '0' || t[1] > '9') return false;
        out = (t[0] - '0') * 10 + (t[1] - '0');
        return true;
    };
    if (!part(s.substr(11, 2), hour)) return false;
    if (!part(s.substr(14, 2), minute)) return false;
    if (!part(s.substr(17, 2), second)) return false;
    if (hour > 23 || minute > 59 || second > 59) return false;
    year = y;
    month = m;
    day = d;
    return true;
}

std::string formatTimestamp(int year, int month, int day, int hour,
                            int minute, int second) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", year, month,
                  day, hour, minute, second);
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
// AssetCategory
// ----------------------------------------------------------------------------

const char *categoryToken(AssetCategory c) {
    switch (c) {
        case AssetCategory::RealEstate: return "REAL_ESTATE";
        case AssetCategory::Vehicle: return "VEHICLE";
        case AssetCategory::Art: return "ART";
        case AssetCategory::Collectible: return "COLLECTIBLE";
        case AssetCategory::Commodity: return "COMMODITY";
        case AssetCategory::Other: return "OTHER";
    }
    return "OTHER";   // unreachable: the enum is closed
}

bool categoryFromToken(const std::string &token, AssetCategory &out) {
    if (token == "REAL_ESTATE") { out = AssetCategory::RealEstate; return true; }
    if (token == "VEHICLE") { out = AssetCategory::Vehicle; return true; }
    if (token == "ART") { out = AssetCategory::Art; return true; }
    if (token == "COLLECTIBLE") { out = AssetCategory::Collectible; return true; }
    if (token == "COMMODITY") { out = AssetCategory::Commodity; return true; }
    if (token == "OTHER") { out = AssetCategory::Other; return true; }
    return false;   // unknown token: the serde rejects it
}

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

bool BalanceSheet::parseAssetLine(const std::string &line, std::string &outId,
                                  Asset &out) {
    const std::vector<std::string> f = SheetSerde::splitLine(line);
    // Set form: kind + 6 fields (no trailer exists for assets).
    if (f.size() != 7) return false;
    if (f[0] != "A") return false;
    outId = f[1];
    if (!validId(outId)) return false;
    const std::string &label = f[2];
    if (!validLabel(label)) return false;
    AssetCategory category;
    if (!categoryFromToken(f[3], category)) return false;   // the serde IS the validator
    double value = 0.0;
    if (!SheetSerde::parseDecimal(f[4], value) || value < 0.0) return false;
    int y, m, d, h, mi, s;
    if (!SheetSerde::parseTimestamp(f[5], y, m, d, h, mi, s)) return false;
    const std::string note = f[6] == "-" ? std::string() : f[6];
    if (note.size() > kMaxLabelLength) return false;

    out = Asset();
    out.id = outId;
    out.label = label;
    out.category = category;
    out.valueEur = value;
    out.updated = f[5];   // the validated form, stored verbatim
    out.note = std::move(note);
    return true;
}

bool BalanceSheet::parseLiabilityLine(const std::string &line, std::string &outId,
                                      Liability &out) {
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
    const std::string kind = line.substr(0, line.find(';'));
    if (kind == "L") {
        std::string id;
        Liability liability;
        if (!parseLiabilityLine(line, id, liability)) {
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
    if (kind == "A") {
        std::string id;
        Asset asset;
        if (!parseAssetLine(line, id, asset)) {
            FileLog::w("BalanceSheet", "setLine rejected an asset line (shape/fields failed validation)");
            return false;
        }
        auto it = std::find_if(assets_.begin(), assets_.end(),
                               [&](const Asset &a) { return a.id == id; });
        if (it != assets_.end()) {
            *it = std::move(asset);   // upsert: replace in place, keep the position
        } else {
            assets_.push_back(std::move(asset));
        }
        return true;
    }
    FileLog::w("BalanceSheet", "setLine rejected a line (kind not activated)");
    return false;
}

bool BalanceSheet::deleteEntry(const std::string &kindId) {
    if (kindId.empty()) return false;
    const std::string id = kindId.substr(1);
    if (kindId[0] == 'L') {
        auto it = std::find_if(liabilities_.begin(), liabilities_.end(),
                               [&](const Liability &l) { return l.id == id; });
        if (it == liabilities_.end()) return false;
        liabilities_.erase(it);
        return true;
    }
    if (kindId[0] == 'A') {
        auto it = std::find_if(assets_.begin(), assets_.end(),
                               [&](const Asset &a) { return a.id == id; });
        if (it == assets_.end()) return false;
        assets_.erase(it);
        return true;
    }
    return false;
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

std::string BalanceSheet::assetLine(const Asset &a) {
    std::string line = "A;";
    line += a.id;
    line += ';';
    line += SheetSerde::escapeField(a.label);
    line += ';';
    line += categoryToken(a.category);
    line += ';';
    line += SheetSerde::formatDecimal(a.valueEur);
    line += ';';
    line += a.updated;
    line += ';';
    line += a.note.empty()
                ? std::string("-")
                : SheetSerde::escapeField(a.note);
    return line;
}

std::string BalanceSheet::lineOf(const Asset &a) {
    return assetLine(a);
}

std::vector<std::string> BalanceSheet::listLines(const std::string &kind, const std::tm &asOf) const {
    std::vector<std::string> lines;
    if (kind == "L") {
        lines.reserve(liabilities_.size());
        for (const auto &l: liabilities_) lines.push_back(lineOf(l, asOf));
    } else if (kind == "A") {
        lines.reserve(assets_.size());
        for (const auto &a: assets_) lines.push_back(assetLine(a));
    }
    // other collections resolve empty until active
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
    for (const auto &a: assets_) {
        if (!text.empty()) text += '\n';
        text += assetLine(a);
    }
    return text;
}

size_t BalanceSheet::parse(const std::string &text) {
    size_t rejected = 0;
    std::vector<Liability> nextLiabilities;
    std::vector<Asset> nextAssets;
    if (!text.empty()) {
        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t nl = text.find('\n', start);
            std::string line = text.substr(
                    start, nl == std::string::npos ? std::string::npos : nl - start);
            start = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
            if (line.empty()) continue;
            const std::string kind = line.substr(0, line.find(';'));
            if (kind == "L") {
                std::string id;
                Liability liability;
                if (parseLiabilityLine(line, id, liability)) {
                    nextLiabilities.push_back(std::move(liability));
                } else {
                    ++rejected;
                    FileLog::e("BalanceSheet",
                               "Sheet section line rejected on load (skipped): " + line);
                }
            } else if (kind == "A") {
                std::string id;
                Asset asset;
                if (parseAssetLine(line, id, asset)) {
                    nextAssets.push_back(std::move(asset));
                } else {
                    ++rejected;
                    FileLog::e("BalanceSheet",
                               "Sheet section line rejected on load (skipped): " + line);
                }
            } else {
                ++rejected;
                FileLog::e("BalanceSheet",
                           "Sheet section line rejected on load (skipped): " + line);
            }
        }
    }
    liabilities_ = std::move(nextLiabilities);
    assets_ = std::move(nextAssets);
    return rejected;
}

double BalanceSheet::assetsTotal() const {
    double total = 0.0;
    for (const auto &a: assets_) total += a.valueEur;
    return total;
}

std::vector<std::string> BalanceSheet::snapshot(double trackedAssetsEur,
                                                const std::tm &asOf) const {
    const double nonFinancial = assetsTotal();
    double liabilities = 0.0;
    for (const auto &l: liabilities_) liabilities += l.balanceAsOf(asOf);
    // SIGNED fields: the TRACKED value (a negative EUR buffer or a
    // negative wallet can make it negative) and the NET (debts above
    // assets). Never clamped, unlike the non-negative line fields (the
    // asset sum and the liability sum are sums of non-negative values).
    const double net = trackedAssetsEur + nonFinancial - liabilities;
    std::string line = "NW;";
    line += SheetSerde::formatDecimalSigned(trackedAssetsEur);
    line += ';';
    line += SheetSerde::formatDecimal(nonFinancial);
    line += ';';
    line += SheetSerde::formatDecimal(liabilities);
    line += ';';
    line += SheetSerde::formatDecimalSigned(net);
    return {line};
}
