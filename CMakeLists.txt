cmake_minimum_required(VERSION 3.26)
project(NF_Tx_Core)

set(CMAKE_CXX_STANDARD 20)

add_library(NF_Tx_Core SHARED library.cpp
        Wallet.h
        FileLog.h
        library.h
        library.cpp
        Wallet.cpp
        Util.h
        DataHolder.cpp
        DataHolder.h
        TransactionParser.cpp
        TransactionParser.h
        TransactionManager.cpp
        TransactionManager.h
        BaseTransaction.cpp
        BaseTransaction.h
        Enums.h
        FileLog.cpp
        TimeSpan.h
        WalletBalance.h
        Price/PriceCache.cpp
        Price/PriceCache.h
        Price/Cache.cpp
        Price/Cache.h
        Price/AssetValue.cpp
        Price/AssetValue.h
        Price/StaticPrices.h)
