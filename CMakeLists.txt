cmake_minimum_required(VERSION 3.26)
project(NF_Tx_Core)

set(CMAKE_CXX_STANDARD 20)

add_library(NF_Tx_Core SHARED library.cpp
        Wallet/Wallet.h
        FileLog.h
        library.h
        library.cpp
        Wallet/Wallet.cpp
        Util/Util.h
        DataHolder.cpp
        DataHolder.h
        TransactionParser.cpp
        TransactionParser.h
        TransactionManager.cpp
        TransactionManager.h
        Transaction/BaseTransaction.cpp
        Transaction/BaseTransaction.h
        Enums.h
        FileLog.cpp
        TimeSpan.h
        Wallet/WalletBalance.h
        Price/PriceCache.cpp
        Price/PriceCache.h
        Price/Cache.cpp
        Price/Cache.h
        Price/AssetValue.cpp
        Price/AssetValue.h
        Price/StaticPrices.h
        Structs.h
        BinaryUtil.h
        Transaction/TransactionStruct.h
        Wallet/WalletStruct.h
        TransactionManager/TMState.cpp
        TransactionManager/TMState.h
        Util/CharUtil.h
        Util/Util.cpp
        Util/CharUtil.cpp
        MagicNumbers.h)
