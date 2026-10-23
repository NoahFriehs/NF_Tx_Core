// Local, time-boxed fuzz loop over the committed CSV fixtures.
//
// OSS-Fuzz-style mutation fuzzing for manual/local use (intentionally NOT a
// ctest entry - no CI surface). Build in a sanitizer tree and run:
//
//   cmake -B build -DCMAKE_BUILD_TYPE=Debug \
//     -DCMAKE_CXX_FLAGS="-g -fsanitize=address,undefined -fno-omit-frame-pointer" \
//     -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
//   ./build/NF_Tx_Core_Fuzz [seed] [iterations]
//
// Each iteration mutates one fixture (truncation, single-byte flips, random
// splices of two lines, or a fully random buffer) and drives it through
// parseFromCsv (CDC + BlockPit) and the TransactionManager pipeline -
// the same entry points the JNI bridge exposes. ASan/UBSan abort on any
// memory error; a clean run prints the iteration count.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "../Structs.h"
#include "../TransactionManager.h"
#include "../TransactionParser.h"

// The 10k-row fixture is NOT fuzzed here: a full wallet build per mutated
// case is disproportionately expensive; it is covered by the dedicated
// budget/perf check instead.
static const char *FIXTURES[4] = {
        "blockpit_export.csv", "cdc_card.csv", "cdc_crypto.csv",
        "kraken_trades.csv"};

static std::vector<std::string> loadLines(const std::string &path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

static void driveOnce(const std::vector<std::string> &lines) {
    // std::invalid_argument throws ("Data is empty" on empty input, an
    // empty transaction set refused by setTransactions) are the DESIGNED
    // abort paths - the JNI entry catches them and surfaces a clean parse
    // failure. Only a CRASH (memory error) matters here: ASan/UBSan.
    try {
        TransactionParser p(lines);
        p.parseFromCsv(Mode::CDC);
        auto tm = std::make_unique<TransactionManager>();
        if (!p.getTransactions().empty()) {
            tm->setTransactions(p.getTransactions(), Mode::CDC);
            tm->processTransactions();
        }
    } catch (const std::exception &) {
    }
    try {
        TransactionParser p(lines);
        p.parseFromCsv(Mode::BlockPit);
        auto tm = std::make_unique<TransactionManager>();
        if (!p.getTransactions().empty()) {
            tm->setTransactions(p.getTransactions(), Mode::BlockPit);
            tm->processTransactions();
        }
    } catch (const std::exception &) {
    }
}

int main(int argc, char **argv) {
    const uint32_t seed = argc > 1 ? static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10))
                                   : 0x5EEDu;
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 20000;
    const std::string dir =
            std::filesystem::path(NF_TX_CORE_TEST_DATA_DIR).string();
    std::vector<std::vector<std::string>> fixtures;
    for (const char *name: FIXTURES) {
        auto lines = loadLines(dir + "/" + name);
        if (!lines.empty()) fixtures.push_back(std::move(lines));
    }
    if (fixtures.empty()) {
        std::printf("FAIL: no fixtures found in %s\n", dir.c_str());
        return 1;
    }
    std::mt19937 rng(seed);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        std::vector<std::string> caseLines = fixtures[rng() % fixtures.size()];
        const int op = rng() % 4;
        if (op == 0) {   // truncate
            const size_t cut = caseLines.size() ? rng() % caseLines.size() : 0;
            caseLines.resize(cut);
        } else if (op == 1 && !caseLines.empty()) {   // single-byte flip
            auto &l = caseLines[rng() % caseLines.size()];
            if (!l.empty()) {
                l[rng() % l.size()] = static_cast<char>(rng() % 256);
            }
        } else if (op == 2 && caseLines.size() >= 2) {   // splice two lines
            auto &l = caseLines[rng() % caseLines.size()];
            const std::string &other = caseLines[rng() % caseLines.size()];
            const size_t pos = rng() % l.size();
            l.insert(pos, other);
        } else {   // random buffer replacing one line
            if (!caseLines.empty()) {
                auto &l = caseLines[rng() % caseLines.size()];
                const size_t len = rng() % 64;
                l.resize(len);
                for (auto &c: l) c = static_cast<char>(rng() % 256);
            }
        }
        driveOnce(caseLines);
        if (i % 5000 == 0 && i > 0) {
            std::printf("...%d iterations\n", i);
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("fuzz: %d iterations, %lld ms, seed %u - ASan/UBSan clean\n",
                iterations,
                static_cast<long long>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                t1 - t0)
                                .count()),
                seed);
    return 0;
}
