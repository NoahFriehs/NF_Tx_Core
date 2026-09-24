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
        TransactionManager/TMState.cpp
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
    add_test(NAME unit_tests COMMAND NF_Tx_Core_Tests)
endif()
