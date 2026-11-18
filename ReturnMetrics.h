#ifndef NF_TX_CORE_RETURN_METRICS_H
#define NF_TX_CORE_RETURN_METRICS_H

#include <string>

//! \brief The portfolio return metrics over a priced daily series - the
//! pure core math (stateless, no DataHolder access, callable from any
//! thread).
//!
//! Input ([series]): one line per calendar day,
//! "R;<YYYY-MM-DD>;<V>;<CF>" with V = the end-of-day portfolio value in
//! EUR and CF = the net EXTERNAL EUR inflow of that day (a deposit
//! positive, a withdrawal negative, 0 allowed). The caller prices the
//! series (the metric values the same prices a chart would - agreement
//! by construction); the core owns only the math and the honesty guards.
//!
//! Output: two lines separated by a single '\n' (empty result for an
//! empty input):
//!   "TWR;<absolute>;<annualized>;<periods>;<skipped>"
//!   "MWR;<absolute>;<annualized>;<spandays>"
//! The values are signed fractions with four decimals (0.2100 = +21 %):
//! TWR = the chain of period returns (a period with V_{t-1} <= 0 is
//! skipped, its count reported in <skipped>); MWR/IRR = the window rate
//! r that grows the initial value and the post-start flows into the end
//! value (the annualized field is the 365-day geometric of the window;
//! <spandays> is the window's calendar length). The core never emits a
//! percentage it cannot back:
//!   - a window of < 30 days, or no computable TWR period, or an
//!     all-zero CF stream (pure drift: the IRR carries nothing beyond
//!     the TWR line next to it), or no MWR root in the bisection
//!     bracket -> the numbers are replaced by the marker:
//!     "TWR;INSUFFICIENT;<periods>;<skipped>" respectively
//!     "MWR;INSUFFICIENT;<spandays>";
//!   - a TWR product <= 0 has no real 365-geometric: the annualized
//!     field carries the marker "-" (product == 0 -> "-1.0000"), the
//!     absolute value is still reported.
class ReturnMetrics {
public:
    //! Compute the two-line metric result of [series] (see the class
    //! doc for the exact line contract). Malformed lines are skipped and
    //! logged (the input is built by the caller); the valid lines are
    //! sorted by date, a repeated day keeps its first line.
    static std::string compute(const std::string &series);
};

#endif // NF_TX_CORE_RETURN_METRICS_H
