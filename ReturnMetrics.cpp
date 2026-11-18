#include "ReturnMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "FileLog.h"

namespace {

//! 365-day geometric annualization (the day-count convention).
constexpr double kYearDays = 365.0;
//! Windows shorter than this never emit a percentage (noise, not
//! signal - the no-silent-figures rule of the display).
constexpr long long kMinSpanDays = 30;
//! The MWR bisection bracket (window rate) and its iteration count:
//! far tighter than the four-decimal display needs, and a bisection has
//! no Newton-mode divergence.
constexpr double kRateLo = -0.999;
constexpr double kRateHi = 10.0;
constexpr int kRootIters = 120;

const char *kTag = "ReturnMetrics";

//! Proleptic-Gregorian day number (Hinnant's civil algorithm): pure
//! integer math, no timezones, no library - the window span never
//! touches a wall clock.
long long daysFromCivil(long long y, long long m, long long d) {
    y -= (m <= 2) ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;                                 // [0, 399]
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    return era * 146097 + doe - 719468;
}

//! Strict "YYYY-MM-DD" validation (calendar-checked, e.g. 2023-02-30
//! is rejected); the day number, or -1 for anything malformed.
long long dayOf(const std::string &key) {
    if (key.size() != 10 || key[4] != '-' || key[7] != '-') return -1;
    for (size_t i = 0; i < 10; i++)
        if (i != 4 && i != 7 && (key[i] < '0' || key[i] > '9')) return -1;
    const int y = std::atoi(key.substr(0, 4).c_str());
    const int m = std::atoi(key.substr(5, 2).c_str());
    const int d = std::atoi(key.substr(8, 2).c_str());
    static const int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12 || d < 1) return -1;
    int dim = kMonthDays[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) dim = 29;
    if (d > dim) return -1;
    return daysFromCivil(y, m, d);
}

//! Full-token double parse: the whole token must be consumed and the
//! value finite (no trailing garbage, no inf/nan).
bool parseDouble(const std::string &token, double *out) {
    if (token.empty()) return false;
    char *end = nullptr;
    const double v = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

//! Signed four-decimal format without a negative zero (the display
//! would read "-0.0000" as a loss).
std::string fmt4(double v) {
    if (v > -0.00005 && v < 0.00005) v = 0.0;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return std::string(buf);
}

//! One parsed input line: the day number + the priced value and the
//! net external flow of that day.
struct DayLine {
    long long day;
    double value;
    double flow;
};

} // namespace

std::string ReturnMetrics::compute(const std::string &series) {
    // Parse + collect the valid lines. The input is built by the caller
    // (well formed by contract); a malformed line is skipped and logged
    // so a builder regression stays visible instead of crashing the
    // metric.
    std::vector<DayLine> lines;
    std::istringstream in(series);
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back(); // CRLF-safe
        if (raw.empty()) continue;
        std::vector<std::string> f;
        size_t start = 0;
        while (true) {
            const size_t pos = raw.find(';', start);
            if (pos == std::string::npos) { f.push_back(raw.substr(start)); break; }
            f.push_back(raw.substr(start, pos - start));
            start = pos + 1;
        }
        DayLine line{-1, 0.0, 0.0};
        if (f.size() != 4 || f[0] != "R" || dayOf(f[1]) < 0 ||
            !parseDouble(f[2], &line.value) || !parseDouble(f[3], &line.flow)) {
            FileLog::w(kTag, "Ignoring malformed return-series line: " + raw);
            continue;
        }
        line.day = dayOf(f[1]);
        lines.push_back(line);
    }
    if (lines.empty()) return {};

    // Chronological order (the line contract is one line per calendar
    // day; a repeated day keeps its FIRST line - defensive only).
    std::vector<size_t> order(lines.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return lines[a].day < lines[b].day;
    });
    std::vector<DayLine> days;
    days.reserve(lines.size());
    for (size_t i: order)
        if (days.empty() || lines[i].day != days.back().day)
            days.push_back(lines[i]);

    const long long spanDays = days.back().day - days.front().day;
    const size_t n = days.size();

    // ---- TWR ---------------------------------------------------------
    // R_t = (V_t - V_{t-1} - CF_t) / V_{t-1}; a period that starts at
    // V_{t-1} <= 0 has no meaningful denominator and is skipped - the
    // count is reported in the line, never hidden. TWR = prod(1 + R_t)
    // - 1, annualized with the 365-geometric over the real span.
    size_t periods = 0;
    size_t skipped = 0;
    double product = 1.0;
    for (size_t i = 1; i < n; i++) {
        if (days[i - 1].value <= 0.0) {
            skipped++;
            continue;
        }
        periods++;
        product *= (days[i].value - days[i].flow) / days[i - 1].value; // 1 + R_t
    }
    std::string twrLine;
    if (spanDays < kMinSpanDays || periods == 0) {
        twrLine = "TWR;INSUFFICIENT;" + std::to_string(periods) + ";" + std::to_string(skipped);
    } else {
        const double absReturn = product - 1.0;
        std::string ann;
        if (product > 0.0)
            ann = fmt4(std::pow(product, kYearDays / static_cast<double>(spanDays)) - 1.0);
        else if (product == 0.0)
            ann = "-1.0000"; // total loss over the window - defined, not marked
        else
            ann = "-"; // base < 0: no real 365-geometric, the absolute stays
        twrLine = "TWR;" + fmt4(absReturn) + ";" + ann + ";" +
                  std::to_string(periods) + ";" + std::to_string(skipped);
    }

    // ---- MWR / IRR ---------------------------------------------------
    // Window rate r (x = 1 + r): the initial value and every post-start
    // flow are grown to the end of the window and must rebuild the end
    // value,
    //   f(x) = V_end - V_0*x - sum_{i>=1} CF_i * x^((d_end - d_i)/D) = 0,
    // D = the window's calendar days. A flow ON the inception line is
    // not in the sum: V_0 already values it (the position after that
    // day's flows IS V_0), only i >= 1 flow after inception. The
    // ALL-zero CF stream (pure drift) is marked, not numbered: with no
    // flow at all the IRR carries nothing beyond the TWR line next to
    // it. Bisection on the window-rate bracket: with mixed flows f is
    // not guaranteed monotone, so the endpoint sign check is the
    // contract - a bracket-less f yields the marker, not a guess.
    const bool allZeroFlows =
            std::all_of(days.begin(), days.end(), [](const DayLine &d) { return d.flow == 0.0; });
    std::string mwrLine;
    if (n < 2 || spanDays < kMinSpanDays || allZeroFlows) {
        mwrLine = "MWR;INSUFFICIENT;" + std::to_string(spanDays);
    } else {
        const long long dEnd = days.back().day;
        const double v0 = days.front().value;
        const double vEnd = days.back().value;
        auto f = [dEnd, spanDays, v0, vEnd, &days](double r) {
            const double x = 1.0 + r;
            double acc = vEnd - v0 * x;
            for (size_t i = 1; i < days.size(); i++)
                acc -= days[i].flow * std::pow(x,
                        static_cast<double>(dEnd - days[i].day) / static_cast<double>(spanDays));
            return acc;
        };
        bool bracketed = f(kRateLo) * f(kRateHi) <= 0.0;
        double lo = kRateLo;
        double hi = kRateHi;
        if (bracketed) {
            double fLo = f(lo);
            for (int it = 0; it < kRootIters; it++) {
                const double mid = 0.5 * (lo + hi);
                const double fMid = f(mid);
                if (fLo * fMid <= 0.0) {
                    hi = mid;
                } else {
                    lo = mid;
                    fLo = fMid;
                }
            }
        }
        if (bracketed) {
            const double root = 0.5 * (lo + hi);
            const double x = 1.0 + root;
            mwrLine = "MWR;" + fmt4(root) + ";" +
                      fmt4(std::pow(x, kYearDays / static_cast<double>(spanDays)) - 1.0) + ";" +
                      std::to_string(spanDays);
        } else {
            mwrLine = "MWR;INSUFFICIENT;" + std::to_string(spanDays);
        }
    }

    return twrLine + "\n" + mwrLine;
}
