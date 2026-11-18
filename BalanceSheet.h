// The user-owned balance-sheet state of the core (ruling: offline
// functionality lives in the core). The sheet holds the collections the
// user maintains by hand - liabilities, non-financial assets, goals and
// price alerts in this build - the core-owned per-goal alert latch -
// plus the core-owned line-serde that is ALSO the v5 save-section
// format: the on-disk section, the C-surface lines and the UI rows are
// the same text, so the app never re-implements the contract.
//
// The sheet is NOT import state: clearAll() (the import-state reset) must
// never touch it. It is stored alongside the import state in the core's
// save (the additive v5 balanceSheet section) and survives every
// init/restore/clearAll sequence the app runs.

#ifndef NF_TX_CORE_BALANCESHEET_H
#define NF_TX_CORE_BALANCESHEET_H

#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

//! The user-declared category of a non-financial asset. The core is the
//! vocabulary owner: the serde line carries one of the six tokens, the
//! parser rejects anything else (the serde IS the validator), and the app
//! picker hardcodes the same six tokens to build and pick - one list, no
//! drift channel.
enum class AssetCategory {
    RealEstate,   //!< "REAL_ESTATE" - home, land, property
    Vehicle,      //!< "VEHICLE" - car, motorcycle, boat, ...
    Art,          //!< "ART" - paintings, sculpture, jewelry
    Collectible,  //!< "COLLECTIBLE" - stamps, coins, cards, figurines
    Commodity,    //!< "COMMODITY" - gold, silver, wine, timber
    Other,        //!< "OTHER" - anything that fits no bucket above
};

//! The serde token of [c] ("REAL_ESTATE", ...).
const char *categoryToken(AssetCategory c);

//! Parse a serde category token (case-sensitive). false = unknown token.
bool categoryFromToken(const std::string &token, AssetCategory &out);

//! The metric a goal tracks (the core owns the two-token vocabulary;
//! the sheet's GP progress lines carry the metric's VALUE, never a
//! recomputation of it - one source of truth, the app renders only).
enum class GoalMetric {
    NetWorth,   //!< "NET_WORTH" - the snapshot's net line
    Assets,     //!< "ASSETS" - the snapshot's tracked-assets line
};

//! The serde token of [m] ("NET_WORTH" / "ASSETS").
const char *metricToken(GoalMetric m);

//! Parse a serde metric token (case-sensitive). false = unknown token.
bool metricFromToken(const std::string &token, GoalMetric &out);

//! The crossing direction of a price alert (the core owns the two-token
//! vocabulary; the serde line carries one of the two case-sensitive
//! tokens, the parser rejects anything else, and the app picker
//! hardcodes the same two - one list, no drift channel).
enum class AlertDirection {
    Above,  //!< "ABOVE" - the condition is "price strictly above the threshold"
    Below,  //!< "BELOW" - the condition is "price strictly below the threshold"
};

//! The serde token of [d] ("ABOVE" / "BELOW").
const char *directionToken(AlertDirection d);

//! Parse a serde direction token (case-sensitive). false = unknown token.
bool directionFromToken(const std::string &token, AlertDirection &out);

//! One user-maintained debt line (the liability collection). The current
//! balance is core-computed (closed-form amortization) and never
//! user-declared - the user enters four numbers, the core owns the math.
struct Liability {
    //! App-generated id (a UUID), the upsert/delete key. Non-empty,
    //! ASCII letters/digits/'-', max 64 chars, no ';'.
    std::string id;
    //! Free user text (escaped as ';;' in the line format).
    std::string label;
    //! Principal at the start, EUR (>= 0).
    double principalEur = 0.0;
    //! Nominal annual rate in percent (>= 0).
    double annualRatePct = 0.0;
    //! Wall-clock start date (the loan's first day), exactly as displayed.
    int startYear = 0;
    int startMonth = 0;
    int startDay = 0;
    //! Scheduled end - hasEndDate = false means an open end.
    bool hasEndDate = false;
    int endYear = 0;
    int endMonth = 0;
    int endDay = 0;
    //! Fixed monthly payment, EUR (0 = no payment: the balance only
    //! compounds).
    double monthlyPaymentEur = 0.0;

    //!
    //! Closed-form amortized balance as of [asOf] (wall clock):
    //!   i  = annualRatePct / 1200                 (monthly rate)
    //!   n  = whole months from the start to asOf, floored at 0; capped at
    //!        the months to the end date when one is set
    //!   past the end date            -> 0
    //!   M > 0, i > 0:  B = P (1+i)^n - M ((1+i)^n - 1) / i
    //!   M > 0, i == 0: B = P - M n                (the i -> 0 limit)
    //!   M == 0,        B = P (1+i)^n   (i == 0 -> P: pure compounding)
    //!   B = max(0, B)                            (an overpay pays off)
    //! Month-level granularity (the day positions do not count); this is a
    //! smooth, hand-fixturable model of the common annuity, not a
    //! banking-grade day-count convention.
    double balanceAsOf(const std::tm &asOf) const;
};

//! One user-maintained goal (the goal collection): a declared target
//! amount on one of the two core metrics. [progress] is core-computed in
//! the snapshot from the metric's current value - the user declares the
//! TARGET, the core owns the math (no contribution projection, no
//! forecast: the target date is a reference point, not an input to any
//! computation in this build).
struct Goal {
    //! App-generated id (a UUID), the upsert/delete key. Same rules as
    //! Liability::id.
    std::string id;
    //! Free user text (escaped as ';;' in the line format).
    std::string label;
    //! The declared target, EUR (>= 0). 0 is degenerate: the GP line
    //! carries the insufficient marker instead of a progress value (the
    //! core never divides by it).
    double targetEur = 0.0;
    //! Optional target date - a reference point the display shows, never
    //! consumed by the core's math.
    bool hasTargetDate = false;
    //! The target date, exactly as displayed ("YYYY-MM-DD", the
    //! validated form, stored verbatim like Asset::updated).
    std::string targetDate;
    //! The metric the goal tracks ("NET_WORTH" / "ASSETS").
    GoalMetric metric = GoalMetric::NetWorth;
};

//! One user-maintained price alert (the alert collection): a threshold
//! on the current EUR price of one held symbol. The CROSSING IS OWNED BY
//! THE CORE (see alertCheck): the [fired] latch is armed/disarmed by the
//! automaton, and the app builds and renders lines only (the UI always
//! builds the line with fired = false = creation or a deliberate
//! re-arm by edit).
struct PriceAlert {
    //! App-generated id (a UUID), the upsert/delete key. Same rules as
    //! Liability::id.
    std::string id;
    //! A held currency symbol (the core's token, e.g. "BTC"): non-empty,
    //! ASCII letters/digits, max 16 chars.
    std::string symbol;
    //! The crossing direction.
    AlertDirection direction = AlertDirection::Above;
    //! The threshold, EUR (>= 0, finite).
    double thresholdEur = 0.0;
    //! The latched-crossing state (see alertCheck).
    bool fired = false;
    //! The wall-clock "YYYY-MM-DDTHH:MM:SS" of the pass that fired (the
    //! asOf of that evaluation); empty = never (the line carries "-").
    std::string firedIso;
};

//! The core-owned goal-achieved latch (keyed per goal id in the sheet):
//! it EXISTS only while its goal is latched as reached - the fired
//! progress is the only non-default state, and it re-arms automatically
//! when the progress drops below 1 (a goal can swing). Core-managed:
//! never user-editable (setLine rejects the "GA" kind), persisted in
//! the save section as the "GA" line while latched.
struct GoalLatch {
    bool fired = false;
    //! The fired stamp ("YYYY-MM-DDTHH:MM:SS" as wall-clock ISO), empty
    //! when unfired.
    std::string firedIso;
};

//! One user-maintained non-financial asset entry (the asset collection).
//! PURE STATIC value: no schedule math - the user re-declares the value
//! when it changes and stamps [updated], the "honesty signal" of how
//! fresh the declared value is (the core never recomputes it).
struct Asset {
    //! App-generated id (a UUID), the upsert/delete key. Same rules as
    //! Liability::id.
    std::string id;
    //! Free user text (escaped as ';;' in the line format).
    std::string label;
    //! The declared category (one of the six tokens, core-validated).
    AssetCategory category = AssetCategory::Other;
    //! User-declared current value, EUR (>= 0).
    double valueEur = 0.0;
    //! Wall-clock "last updated" stamp, local time, the validated
    //! "YYYY-MM-DDTHH:MM:SS" form (the serde stores it verbatim).
    std::string updated;
    //! Optional note (escaped; empty = none, "-" in the line).
    std::string note;
};

class BalanceSheet {
public:
    //! The liability collection (active; input order, which the line list
    //! keeps).
    const std::vector<Liability> &getLiabilities() const { return liabilities_; }
    std::vector<Liability> &mutableLiabilities() { return liabilities_; }

    //! The non-financial asset collection (active; input order).
    const std::vector<Asset> &getAssets() const { return assets_; }
    std::vector<Asset> &mutableAssets() { return assets_; }

    //! The goal collection (active; input order).
    const std::vector<Goal> &getGoals() const { return goals_; }
    std::vector<Goal> &mutableGoals() { return goals_; }

    //! The price alert collection (active; input order).
    const std::vector<PriceAlert> &getPriceAlerts() const { return priceAlerts_; }
    std::vector<PriceAlert> &mutablePriceAlerts() { return priceAlerts_; }

    //! The core-owned goal-achieved latches (goal id -> latch). A latch
    //! EXISTS only while its goal is latched as reached (the fired state
    //! is the only non-default state; it re-arms automatically when the
    //! progress drops below 1). Core-managed (never user-editable -
    //! setLine rejects the "GA" kind).
    const std::map<std::string, GoalLatch> &getGoalLatches() const { return goalLatches_; }

    //! The sum of the declared asset values (the non-financial part of
    //! the net-worth snapshot).
    double assetsTotal() const;

    //!
    //! The net-worth snapshot - the read-only contract the net-worth
    //! screen shows. Consumers dispatch on the KIND PREFIX, never the
    //! position (each collection appends its lines after the NW line):
    //!   NW;<trackedAssets>;<nonFinancial>;<liabilities>;<net>
    //! followed by one GP line per goal, in stored goal order:
    //!   GP;<id>;<current>;<target>;<progress|->;<reached|->>
    //! [trackedAssetsEur] = the core's own current EUR wallet value, the
    //! internal "EUR" cash buffer INCLUDED - injected by the caller (the
    //! sheet holds no wallet state of its own; the buffer-inclusion
    //! semantic is core-pinned at the injection point, so the display can
    //! never drift from it). SIGNED: a negative buffer or a negative
    //! wallet can make it negative; [nonFinancial] = assetsTotal();
    //! [liabilities] = the sum of the closed-form current balances at
    //! [asOf]; [net] = tracked + nonFinancial - liabilities (SIGNED -
    //! debts above assets stay negative, never clamped).
    //! GP line fields: [current] = the goal's METRIC value (NET_WORTH =
    //! the net field, ASSETS = the tracked value - both SIGNED); [target]
    //! as declared; [progress] = current / target (SIGNED, unclamped),
    //! OR "-" for a degenerate target (<= 0: the insufficient marker -
    //! the core never divides); [reached] = "reached" when progress >= 1,
    //! "-" otherwise (and on a degenerate target: no progress, no
    //! crossing).
    std::vector<std::string> snapshot(double trackedAssetsEur,
                                      const std::tm &asOf) const;

    //! The edge-triggered alert check (the latched crossing automaton,
    //! core-owned and fixture-pinned). For every price alert with latch
    //! [fired] and current price [L] (from [currentPrice]; false = the
    //! core has no current price for the symbol - a symbol no longer
    //! held - so the alert is SKIPPED ENTIRELY: untouched, never firing
    //! or disarming, no invented "last price"):
    //!   ABOVE: cond = L > threshold     BELOW: cond = L < threshold
    //!   !cond          -> fired = false, fired stamp cleared (disarm)
    //!   cond && !fired -> fire: fired = true, fired stamp = [asOf],
    //!                     emit a FIRED line (shape below)
    //! A condition already TRUE at creation therefore fires EXACTLY ONCE
    //! on the first pass (latched first-true - the rejected
    //! crossings-only alternative would never fire it). For every goal,
    //! the goal-achieved state keyed per goal id: cond = the snapshot's
    //! GP condition on the goal's metric over the SAME values (NET_WORTH
    //! reads net = [trackedAssetsEur] + the non-financial sum - the
    //! liability balances at [asOf], ASSETS reads [trackedAssetsEur] -
    //! never a recomputation); the latch re-arms automatically when the
    //! progress drops below 1 (a goal can swing - no user edit needed),
    //! a degenerate target (<= 0) has no crossing and no latch.
    //! The FIRED lines are the ONLY part of this check that crosses the
    //! C boundary - the state, the prices and the math never do:
    //!   FIRED;PRICE;<id>;<symbol>;<ABOVE|BELOW>;<threshold>;<price>
    //!   FIRED;GOAL;<id>;<label>;<metric>;<current>;<target>
    //! ([label] is ';;'-escaped like every sheet text field; [current]
    //! is SIGNED, formatted like the snapshot's values.)
    //! [stateChangedOut] = true when ANY latch changed (a fire OR a
    //! disarm - both are persistable sheet state). Latches of goals that
    //! no longer exist are pruned (defensive: a hand-edited store) and
    //! count as a change.
    std::vector<std::string> alertCheck(
            double trackedAssetsEur,
            const std::function<bool(const std::string &, double &)> &currentPrice,
            const std::tm &asOf, bool &stateChangedOut);

    //!
    //! The line-serde (the house string-line contract, core-owned):
    //!   L;<id>;<label>;<principalEur>;<annualRatePct>;<start>;<end|->;<payment|->
    //! optionally followed by the core-computed trailer
    //!   ;bal;<balanceEur>
    //! (recomputed on emit, ignored on set, so a list line round-trips
    //! through set as-is). Fields:
    //!   id      - non-empty, ASCII [A-Za-z0-9-], max 64, no ';'
    //!   label   - any UTF-8 text, max 512 chars; a ';' is escaped as ';;'
    //!             (the single escape rule of the line format)
    //!   numbers - decimal values >= 0 (finite), '-' for the optional
    //!             end date / payment
    //!   dates   - YYYY-MM-DD, a real calendar day (validated)
    //! The non-financial asset line (kind "A", also active in this
    //! build; no trailer - nothing is core-computed for a static value):
    //!   A;<id>;<label>;<category>;<valueEur>;<updatedISO>;<note|->
    //!     category  - one of the six AssetCategory tokens (case-
    //!                 sensitive; anything else is rejected)
    //!     updatedISO- YYYY-MM-DDTHH:MM:SS, a real local wall-clock time
    //!     note      - escaped text, max 512 chars, "-" for none
    //! The goal line (kind "G", also active in this build; no trailer -
    //! the progress lives in the snapshot, not the line):
    //!   G;<id>;<label>;<targetEur>;<targetDate|->;<metric>
    //!     targetEur  - decimal >= 0 (finite); 0 is legal but degenerate
    //!     targetDate - YYYY-MM-DD (validated) or "-" for none
    //!     metric     - "NET_WORTH" or "ASSETS" (case-sensitive; anything
    //!                  else is rejected)
    //! The KIND PREFIX selects the collection; this build activates
    //! "L", "A" and "G". setLine is a validator-fronted UPSERT BY id: a
    //! rejected line (bad shape/text/number/date/category, or a kind this
    //! build does not activate) leaves the sheet completely untouched and
    //! returns false.
    bool setLine(const std::string &line);

    //! Delete the entry [kindId] = kind prefix + id (as entered in the
    //! line, e.g. "L<uuid>" or "A<uuid>"). false when the kind or the
    //! entry is unknown.
    bool deleteEntry(const std::string &kindId);

    //! The current entry lines of [kind] ("L", "A" and "G" now; other
    //! kinds resolve to the empty list until their phase activates
    //! them), in stored order, the trailing ;bal; value computed at
    //! [asOf] (assets and goals carry no trailer).
    std::vector<std::string> listLines(const std::string &kind, const std::tm &asOf) const;

    //! The full save-section text: every collection, one line per entry,
    //! '\n'-joined, the same shape as listLines (all kinds, so a later
    //! phase adds entries WITHOUT a format change). Empty sheet = "".
    std::string serialize(const std::tm &asOf) const;

    //! Parse the save-section text (inverse of serialize; lines are
    //! dispatched by kind prefix, order-independent). Returns the number
   //! of REJECTED lines (logged in the cpp; the good lines load). The
    //! parse replaces the previous content.
    size_t parse(const std::string &text);

    //! The line of one entry (same shape as listLines uses).
    static std::string lineOf(const Liability &l, const std::tm &asOf);

    //! Serialize one entry WITHOUT the balance trailer (the set form).
    static std::string lineOf(const Liability &l);

    //! The line of one asset (no trailer possible - nothing is
    //! core-computed).
    static std::string lineOf(const Asset &a);

    //! The line of one goal (no trailer - the progress is core-computed
    //! in the snapshot, not the line).
    static std::string lineOf(const Goal &g);

private:
    //! The current liability as a line (the public single-entry form).
    static std::string liabilityLine(const Liability &l, const std::tm &asOf);
    //! The current asset as a line (the single-entry form).
    static std::string assetLine(const Asset &a);
    //! Parse + validate one liability (the single source of truth; shared
    //! by setLine and parse). false on a bad line, out untouched
    //! semantics.
    static bool parseLiabilityLine(const std::string &line, std::string &outId,
                                   Liability &out);
    //! Parse + validate one asset (same contract for the "A" kind).
    static bool parseAssetLine(const std::string &line, std::string &outId,
                               Asset &out);
    //! Parse + validate one goal (same contract for the "G" kind).
    static bool parseGoalLine(const std::string &line, std::string &outId,
                              Goal &out);
    //! The current goal as a line (the single-entry form).
    static std::string goalLine(const Goal &g);
    //! The current alert as a line (the single-entry form).
    static std::string alertLine(const PriceAlert &a);
    //! Parse + validate one alert (the single source of truth; shared by
    //! setLine and parse). false on a bad line.
    static bool parseAlertLine(const std::string &line, std::string &outId,
                               PriceAlert &out);
    //! The goal latch as a line (emitted ONLY while latched):
    //! "GA;<goalId>;1;<firedISO>".
    static std::string goalLatchLine(const std::string &goalId,
                                     const std::string &firedIso);
    //! Parse + validate one goal latch line (core-owned state; see
    //! goalLatchLine for the shape). false on a bad line.
    static bool parseGoalLatchLine(const std::string &line, std::string &outGoalId,
                                   std::string &outFiredIso);

    std::vector<Liability> liabilities_;
    std::vector<Asset> assets_;
    std::vector<Goal> goals_;
    std::vector<PriceAlert> priceAlerts_;
    std::map<std::string, GoalLatch> goalLatches_;
};

// The line-format text helpers (the single escape rule, both directions).
namespace SheetSerde {

//! Escape a text field for the line format (';' -> ';;').
std::string escapeField(const std::string &s);

//! Split one line on its UNESCAPED field separators. A lone ';' ends the
//! field, a ';;' is a literal ';' inside it - the inverse of escaping.
std::vector<std::string> splitLine(const std::string &line);

//! Strict decimal parse: the WHOLE token must be a finite number.
bool parseDecimal(const std::string &s, double &out);

//! Strict wall-clock date parse (YYYY-MM-DD, real calendar day).
bool parseDate(const std::string &s, int &year, int &month, int &day);

//! Format wall-clock date (the inverse of parseDate).
std::string formatDate(int year, int month, int day);

//! Format a non-negative decimal for the line (10 significant digits -
//! exact for any user-entered money/rate value, re-parses to the same
//! double).
std::string formatDecimal(double v);

//! Format a SIGNED decimal for the line (same 10-significant-digit form,
//! no negative clamp - the net-worth values may legitimately be negative
//! (the tracked value and the net); non-finite input still formats as
//! "0").
std::string formatDecimalSigned(double v);

//! Strict wall-clock timestamp parse (YYYY-MM-DDTHH:MM:SS, a real
//! calendar day and clock time - the asset "updated" field).
bool parseTimestamp(const std::string &s, int &year, int &month, int &day,
                    int &hour, int &minute, int &second);

//! Format the inverse of parseTimestamp.
std::string formatTimestamp(int year, int month, int day, int hour,
                            int minute, int second);

//! Today as a wall-clock tm (the asOf the callers pass for "now").
std::tm todayWallClock();

} // namespace SheetSerde

#endif //NF_TX_CORE_BALANCESHEET_H
