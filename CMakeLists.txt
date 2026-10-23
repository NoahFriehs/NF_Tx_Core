cmake_minimum_required(VERSION 3.22.1)
project(NF_Tx_Core)

set(CMAKE_CXX_STANDARD 20)

# The static core library is also linked into the shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(NF_TX_CORE_SOURCES
        DataHolder.cpp
        FileLog.cpp
        Price/AssetValue.cpp
        Price/Cache.cpp
        Price/PriceCache.cpp
        Transaction/BaseTransaction.cpp
        TransactionManager.cpp
        TransactionParser.cpp
        Util/Util.cpp
        Util/CharUtil.cpp
        Wallet/Wallet.cpp)

# Internal static library (linked by the shared lib and the unit tests)
add_library(NF_Tx_Core_core STATIC ${NF_TX_CORE_SOURCES})

# Shared library with the C/JNI API (used by the tester and the Android app)
add_library(NF_Tx_Core SHARED library.cpp)
target_link_libraries(NF_Tx_Core PRIVATE NF_Tx_Core_core)

option(NF_TX_CORE_BUILD_TESTS "Build the unit tests" ON)
if(NF_TX_CORE_BUILD_TESTS)
    enable_testing()
    add_executable(NF_Tx_Core_Tests Tests/tests.cpp)
    target_link_libraries(NF_Tx_Core_Tests PRIVATE NF_Tx_Core_core)
    target_compile_definitions(NF_Tx_Core_Tests PRIVATE
        NF_TX_CORE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")
    add_test(NAME unit_tests COMMAND NF_Tx_Core_Tests)

    # Regenerates Tests/fixtures/blockpit_10k.csv (deterministic - the
    # committed bytes are reproducible from this tool alone).
    add_executable(NF_Tx_Core_GenBlockPit10K Tests/gen_blockpit_10k.cpp)

    # 10k-row parse budget/perf check (separate ctest: the timing cap and
    # the ground-truth wallet totals are asserted here, in their own
    # process, so the unit suite run stays fast on every CI build).
    add_executable(NF_Tx_Core_BigFixture Tests/big_fixture_check.cpp)
    target_link_libraries(NF_Tx_Core_BigFixture PRIVATE NF_Tx_Core_core)
    target_compile_definitions(NF_Tx_Core_BigFixture PRIVATE
        NF_TX_CORE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")
    add_test(NAME big_fixture_10k COMMAND NF_Tx_Core_BigFixture)
    set_tests_properties(big_fixture_10k PROPERTIES
        PASS_REGULAR_EXPRESSION "big_fixture_10k: [0-9]+ checks, 0 failures")

    # Local, seedable mutation fuzz loop (manual use: run in a sanitizer
    # build; deliberately not a ctest entry - no CI surface).
    add_executable(NF_Tx_Core_Fuzz Tests/fuzz.cpp)
    target_link_libraries(NF_Tx_Core_Fuzz PRIVATE NF_Tx_Core_core)
    target_compile_definitions(NF_Tx_Core_Fuzz PRIVATE
        NF_TX_CORE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")
endif()
