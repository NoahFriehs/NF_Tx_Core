#ifndef NF_TX_CORE_STATICPRICES_H
#define NF_TX_CORE_STATICPRICES_H

#include <string>
#include <unordered_map>

//! Read-only offline fallback table: EUR prices for symbols that have no
//! live price yet. The cache-aware price pass always wins (see
//! AssetValue::getPrice); this table only feeds symbols never cached.
//! Unknown symbols deliberately price at 0.0 (getPrice) - inventing any
//! non-zero price would silently inflate wallet totals.
//!
//! Single source since 2026-10-18: the tester's superset table (~143
//! symbols) was promoted here; the tester includes this file instead of a
//! local copy (see its CMake NF_TX_CORE_DIR variable).

class StaticPrices {
public:
    // EUR per 1 unit of the symbol.
    std::unordered_map<std::string, double> prices;

    StaticPrices() {
        prices["AAX"] = 0.085;
        prices["ADA"] = 0.217898;
        prices["ADS"] = 0.028;
        prices["AGENT"] = 0.018;
        prices["AI"] = 7.9e-05;
        prices["AIAGENT"] = 0.032;
        prices["AIC"] = 0.015;
        prices["AIT"] = 0.018;
        prices["ALGO"] = 0.115713;
        prices["AMZN"] = 0.085;
        prices["APP"] = 0.028;
        prices["ARTY"] = 0.022;
        prices["ATR"] = 0.012;
        prices["BCH"] = 279.31;
        prices["BEER"] = 0.008;
        prices["BEFI"] = 0.000218;
        prices["BENDOG"] = 0.008;
        prices["BEST"] = 0.000683;
        prices["BLAZE"] = 0.012;
        prices["BNB"] = 691.28;
        prices["BONK"] = 3e-06;
        prices["BOOST"] = 0;
        prices["BRETT"] = 9.9e-05;
        prices["BTC"] = 75352.0;
        prices["BUSD"] = 6.36;
        prices["CAM"] = 0.008;
        prices["CAT"] = 0.0;
        prices["CC"] = 0.0009;
        prices["CCD"] = 0.012;
        prices["COOKIE"] = 0.011566;
        prices["CREO"] = 0.018;
        prices["CRO"] = 0.058515;
        prices["CUDOS"] = 0.001588;
        prices["DCB"] = 0.012;
        prices["DEAI"] = 0.022;
        prices["DEFI"] = 0.000102;
        prices["DESCI"] = 2.2e-05;
        prices["DEVVE"] = 0.00135;
        prices["DOGE"] = 0.08276;
        prices["DOT"] = 1.075;
        prices["DUEL"] = 0.000124;
        prices["EGO"] = 0.000209;
        prices["EMT"] = 0.022;
        prices["ESE"] = 0.018;
        prices["ETH"] = 2380.91;
        prices["ETHW"] = 0.250937;
        prices["EUR"] = 1.0;   // fiat: keeps the empty-cache EUR fallback valued
        prices["EV"] = 0.012;
        prices["EXVG"] = 0.028;
        prices["EYWA"] = 0.000308;
        prices["F3"] = 0.012;
        prices["FCON"] = 0.018;
        prices["FOMO"] = 0.003894;
        prices["FPS"] = 0.012;
        prices["FRBK"] = 0.022;
        prices["FUN"] = 1.3e-05;
        prices["FURY"] = 0.025;
        prices["GOAL"] = 0.000294;
        prices["GOATS"] = 1.6e-05;
        prices["GODL"] = 0.000211;
        prices["GPT"] = 0.015;
        prices["GTAI"] = 0.028;
        prices["HAI"] = 0.018;
        prices["HAPPY"] = 0.085;
        prices["HGPT"] = 0.022;
        prices["HIPPO"] = 0.015;
        prices["HOME"] = 0.004994;
        prices["ICE"] = 6.4e-05;
        prices["ICNT"] = 0.012;
        prices["INFRA"] = 0.028;
        prices["KIMA"] = 0.004265;
        prices["KIP"] = 1.6e-05;
        prices["LAI"] = 0.012;
        prices["LINGO"] = 0.013476;
        prices["LKI"] = 0.022;
        prices["LL"] = 0.012;
        prices["LOE"] = 0.018;
        prices["LTC"] = 62.02;
        prices["LUNC"] = 4.7e-05;
        prices["LUNR"] = 0.008;
        prices["LVLY"] = 0.035;
        prices["LYNX"] = 4.5e-05;
        prices["LYX"] = 0.012;
        prices["Laptop"] = 1.0;   // non-crypto asset (FTX laptop credit in BlockPit exports)
        prices["MBG"] = 0.012;
        prices["MEW"] = 0.000473;
        prices["MLC"] = 0.018;
        prices["MON"] = 0.022;
        prices["MPC"] = 0.015;
        prices["MRSOON"] = 0.022;
        prices["MYRO"] = 0.002052;
        prices["NEIRO"] = 8e-05;
        prices["NFT"] = 0.00000025;
        prices["NIGHT"] = 7.9e-05;
        prices["NOS"] = 0.01114;
        prices["NOTAI"] = 0.028;
        prices["NPC"] = 1.7e-05;
        prices["O4DX"] = 0.018;
        prices["OOB"] = 2e-06;
        prices["ORFY"] = 0.028;
        prices["PHIL"] = 0.000202;
        prices["PIXFI"] = 0.018;
        prices["POL"] = 0.085;
        prices["QBX"] = 0.000667;
        prices["RBNT"] = 0.12;
        prices["REACH"] = 0.000124;
        prices["RFRM"] = 0.028;
        prices["RVV"] = 0.022;
        prices["SDM"] = 0.012;
        prices["SENT"] = 0.018;
        prices["SERSH"] = 0.0085;
        prices["SHIB"] = 5e-06;
        prices["SIDUS"] = 5e-06;
        prices["SNEK"] = 0.000553;
        prices["SOL"] = 106.26;
        prices["SPC"] = 0.018;
        prices["SQD"] = 3e-06;
        prices["STEEL"] = 0.000717;
        prices["STONKBROKER"] = 0.005249;
        prices["SUIAI"] = 0.000103;
        prices["SUNDOG"] = 0.002563;
        prices["SUPRA"] = 0.000144;
        prices["SYNT"] = 0.028;
        prices["TAI"] = 0.019295;
        prices["TREE"] = 0.018;
        prices["TRIO"] = 0.012;
        prices["TRX"] = 0.299194;
        prices["UNIT0"] = 0.002272;
        prices["USDT"] = 0.888247;
        prices["VFY"] = 0.018;
        prices["VIA"] = 0.012;
        prices["VSN"] = 0.03897;
        prices["VVS"] = 0.085;
        prices["WIFI"] = 0.000421;
        prices["WOD"] = 0.015;
        prices["XAU"] = 0.0;
        prices["XCAD"] = 0.085;
        prices["XFI"] = 0.022;
        prices["XMR"] = 491.3;
        prices["XRD"] = 0.012;
        prices["XRP"] = 1.32;
        prices["XSWAP"] = 1.9e-05;
        prices["ZIG"] = 0.018;
        prices["xVVS"] = 0.0;
        // Intentionally 0.0 (not parse/price errors, no trustworthy market
        // price - zero is the safe answer): BOOST, CAT, XAU (gold; CoinGecko
        // name mismatch), xVVS (CoinGecko returns a wrong price).
        // Suspicious values kept as-is until re-verified: BUSD (a dollar
        // stablecoin; likely a CoinGecko name mismatch) and NFT (name match
        // dubious - too high for the token the export intends).
    }
};

//! Offline lookup: the table price, 0.0 for unknown symbols (never an
//! invented price).
inline double getPrice(const std::string &symbol) {
    StaticPrices staticPrices;
    auto it = staticPrices.prices.find(symbol);
    return it != staticPrices.prices.end() ? it->second : 0.0;
}

#endif // NF_TX_CORE_STATICPRICES_H
