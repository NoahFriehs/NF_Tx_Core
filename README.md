# NF_Tx_Core

C++20 core library behind the **CDCsvParser** Android app: the single
parser + wallet engine for bank / card / crypto CSV exports (Crypto.com
family, BlockPit, Kraken, custom and default formats). The Android app
compiles these same sources directly (JNI bridge in `library.cpp`); the
standalone build serves the unit-test suite and the CLI tester.

## What it does

- **CSV parsing** — header-driven app-type detection, UTF-8 BOM safe,
  per-line error collection (`lastFailedLines` surfaces the skipped
  unparsable lines instead of failing the whole import)
- **Wallet cards & ledgers** — balances, EUR-valued assets, money spent,
  P/L and rewards, computed by replaying per-wallet ledgers chronologically
- **Series APIs (consumed by the app charts)** — `getDailySeries`
  (spent / value / P/L / rewards per day, card scope, valued at current
  prices), `getDailyWalletSeries` (per-wallet running token
  balance/bonus, *unpriced* — the app applies point-in-time prices itself)
  and the sub-daily twins `getSubDailySpentSeries` / `getSubDailyWallet
  Series` (the intraday frames)
- **Persistence** — portable v3 binary format (explicit byte layout,
  atomic `.tmp`+rename writes; v2 files still read and upgraded on save)
  plus XML import/export
- **Prices** — provider interface, on-disk cache, and the offline static
  fallback table `Price/StaticPrices.h` (144 entries — single source, the
  tester includes this file instead of a local copy)
- **Parse budget** — a caller-configured thread_local deadline
  (`setParseBudgetMs`; `0` = disabled; the Android app sets 120 s per
  import) checked in the parser loops and wallet build; an aborted parse
  is a clean failure

## Layout (abridged)

| Path | Contents |
|---|---|
| `library.cpp` / `library.h` | C/JNI API (the Android `CoreService` natives + the tester's dlsym surface) — the 3 series accessors are declared in library.h's extern "C" block and exported under plain C names |
| `Transaction/` | parsers + the C++ transaction model |
| `Wallet/` | wallet / card ledgers + series building |
| `Price/` | price providers, caches, static fallback table |
| `DataHolder.cpp`, `TransactionManager.cpp`, `TransactionParser.cpp` | process state, ledger engine, entry-point parsing |
| `Tests/` | `tests.cpp` (`NF_Tx_Core_Tests`) · `big_fixture_check.cpp` (`NF_Tx_Core_BigFixture`, the 10k-row budget/perf check) · `gen_blockpit_10k.cpp` (`NF_Tx_Core_GenBlockPit10K`, the deterministic fixture generator) · `fuzz.cpp` (`NF_Tx_Core_Fuzz`, local mutation loop — NOT a ctest entry) · `fixtures/` (5 synthetic CSVs; only `blockpit_export.csv` is CRLF; `blockpit_10k.csv` is committed + fully reproducible by the generator) |

## Build & test

```bash
# unit tests (ASan/UBSan build recommended): 837 checks, 0 failures (2026-11-09)
cmake -S . -B cmake-build-tests -DCMAKE_BUILD_TYPE=Debug && cmake --build -B cmake-build-tests -j8
cd cmake-build-tests && ctest --output-on-failure   # 2 entries: unit_tests + big_fixture_10k
# local fuzz (manual, no CI surface; run in the sanitizer build):
#   ./NF_Tx_Core_Fuzz <seed> <iterations>   # e.g. NF_Tx_Core_Fuzz 7 10000 (~10 s)
#   mutates the 4 small fixtures (the 10k fixture is covered by its own
#   budget ctest, not the fuzz loop)

# shared library the tester dlopens at runtime:
cmake -S . -B cmake-build-debug && cmake --build -B cmake-build-debug -j8
# -> cmake-build-debug/libNF_Tx_Core.so
```

The Android app's `app/src/main/cpp/CMakeLists.txt` is a hand-maintained
NDK variant of this build (C++20, 16 KB page alignment) — it is never
synced from this repo and never copied back.

## CI

GitHub Actions (`ci.yml`): ASan/UBSan build + `ctest` (both entries)
+ a plain Release shared-library build + the tester's own `ctest`
(`NF_Tx_Core_Tester`, checked out next to it) running the core's
fixtures through the app's exact call path on that Release library.

## License

Shield: [![CC BY-NC-ND 4.0][cc-by-nc-nd-shield]][cc-by-nc-nd]

This work is licensed under a
[Creative Commons Attribution Non Commercial No Derivatives 4.0 International License][cc-by-nc-nd].

[![CC BY-NC-ND 4.0][cc-by-nc-nd-image]][cc-by-nc-nd]

[cc-by-nc-nd]: http://creativecommons.org/licenses/by-nc-nd/4.0/

[cc-by-nc-nd-image]: https://licensebuttons.net/l/by-nc-nd/4.0/88x31.png

[cc-by-nc-nd-shield]: https://img.shields.io/badge/License-CC%20BY--NC--ND%204.0-lightgrey.svg
