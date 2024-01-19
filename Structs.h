//
// Created by nfriehs on 12/1/23.
//

#ifndef NF_TX_CORE_STRUCTS_H
#define NF_TX_CORE_STRUCTS_H

#include <string>
#include <ctime>
#include <sstream>
#include <memory>
#include "Enums.h"
#include "XML/rapidxml.hpp"
#include "XML/rapidxml_print.hpp"
#include "XML/rapidxml_utils.hpp"
#include "MagicNumbers.h"
#include "Wallet/WalletStruct.h"

struct TransactionData {
    int transactionId{};
    int walletId = -1;
    int fromWalletId{};
    std::string description = {};
    std::tm transactionDate{};
    std::string currencyType = {};
    std::string toCurrencyType = {};
    long double amount{};
    long double toAmount{};
    long double nativeAmount{};
    long double amountBonus{};
    int transactionTypeOrdinal = NONE;
    std::string transactionTypeString = {};
    std::string transactionHash = {};
    bool isOutsideTransaction = false;
    std::string notes = {};

    [[nodiscard]] TransactionStruct getTransactionStruct() const {
        TransactionStruct transactionStruct;
        transactionStruct.transactionId = transactionId;
        transactionStruct.walletId = walletId;
        transactionStruct.fromWalletId = fromWalletId;
        transactionStruct.description = description;
        transactionStruct.transactionDate = transactionDate;
        transactionStruct.currencyType = currencyType;
        transactionStruct.toCurrencyType = toCurrencyType;
        transactionStruct.amount = amount;
        transactionStruct.toAmount = toAmount;
        transactionStruct.nativeAmount = nativeAmount;
        transactionStruct.amountBonus = amountBonus;
        transactionStruct.transactionType = static_cast<TransactionType>(transactionTypeOrdinal);
        transactionStruct.transactionTypeString = transactionTypeString;
        transactionStruct.transactionHash = transactionHash;
        transactionStruct.isOutsideTransaction = isOutsideTransaction;
        transactionStruct.notes = notes;
        return transactionStruct;
    }

    [[nodiscard]] std::string serializeToXml() const {
        return serializeToXml(*this);
    }

    //! Utility function to serialize a TransactionData struct to XML
    static std::string serializeToXml(const TransactionData &transaction) {
        rapidxml::xml_document<> doc;

        auto *root = doc.allocate_node(rapidxml::node_element, "TransactionData");
        doc.append_node(root);

        // Helper function to add a new node with a value to the root
        auto addNode = [&](const std::string &nodeName, const std::string &value) {
            auto *node = doc.allocate_node(rapidxml::node_element, nodeName.c_str(), value.c_str());
            root->append_node(node);
        };

        // Add nodes for each field in the TransactionData struct
        addNode("transactionId", std::to_string(transaction.transactionId));
        addNode("walletId", std::to_string(transaction.walletId));
        addNode("fromWalletId", std::to_string(transaction.fromWalletId));
        addNode("description", transaction.description);

        // Format the date and time as a string
        char dateTimeStr[20];
        std::strftime(dateTimeStr, sizeof(dateTimeStr), "%Y-%m-%d %H:%M:%S",
                      &transaction.transactionDate);
        std::string dateTimeString = std::string(dateTimeStr);
        addNode("transactionDate", dateTimeString);

        addNode("currencyType", transaction.currencyType);
        addNode("toCurrencyType", transaction.toCurrencyType);
        addNode("amount", std::to_string(transaction.amount));
        addNode("toAmount", std::to_string(transaction.toAmount));
        addNode("nativeAmount", std::to_string(transaction.nativeAmount));
        addNode("amountBonus", std::to_string(transaction.amountBonus));
        addNode("tTO", std::to_string(transaction.transactionTypeOrdinal));
        addNode("tTS", transaction.transactionTypeString);
        addNode("transactionHash", transaction.transactionHash);
        addNode("outTx", transaction.isOutsideTransaction ? "true" : "false");
        addNode("notes", transaction.notes);

        std::string xmlString;
        rapidxml::print(std::back_inserter(xmlString), doc, 0);
        return xmlString;
    }

    //! Utility function to remove the XML header from a string
    static void removeXmlHeader(std::string &xmlContent) {
        size_t headerStart = xmlContent.find("<?xml version='");
        if (headerStart != std::string::npos) {
            size_t headerEnd = xmlContent.find("?>", headerStart);
            xmlContent.erase(headerStart, headerEnd - headerStart + 2); // +2 to remove '?>'
        }
    }

    //! Utility function to deserialize a TransactionData struct from XML
    void deserializeFromXml(const std::string &xml) {
        size_t pos = 0;

        std::string xmlContent = xml;
        removeXmlHeader(xmlContent);

        auto getTagValue = [&](const std::string &tag) -> std::string {
            size_t startTag = xmlContent.find("<" + tag + ">", pos);
            if (startTag == std::string::npos) {
                return "";
            }

            size_t endTag = xmlContent.find("</" + tag + ">", startTag);
            if (endTag == std::string::npos) {
                return "";
            }

            //pos = endTag + 1;
            return xmlContent.substr(startTag + tag.length() + 2, endTag - startTag - tag.length() - 2);
        };

        transactionId = std::stoi(getTagValue("transactionId"));
        walletId = std::stoi(getTagValue("walletId"));
        fromWalletId = std::stoi(getTagValue("fromWalletId"));
        description = getTagValue("description");

        std::string dateTimeStr = getTagValue("transactionDate");
        std::sscanf(dateTimeStr.c_str(), "%d-%d-%d %d:%d:%d",
                    &transactionDate.tm_year, &transactionDate.tm_mon,
                    &transactionDate.tm_mday, &transactionDate.tm_hour,
                    &transactionDate.tm_min, &transactionDate.tm_sec);
        transactionDate.tm_year -= 1900; // Adjust year
        transactionDate.tm_mon -= 1;    // Adjust month

        currencyType = getTagValue("currencyType");
        toCurrencyType = getTagValue("toCurrencyType");
        amount = std::stold(getTagValue("amount"));
        toAmount = std::stold(getTagValue("toAmount"));
        nativeAmount = std::stold(getTagValue("nativeAmount"));
        amountBonus = std::stold(getTagValue("amountBonus"));
        transactionTypeOrdinal = std::stoi(getTagValue("tTO"));
        transactionTypeString = getTagValue("tTS");
        transactionHash = getTagValue("transactionHash");
        std::string outTx = getTagValue("outTx");
        isOutsideTransaction = (outTx == "true");
        notes = getTagValue("notes");
    }


};

struct WalletData {
    int walletId{};
    std::string currencyType = {};
    double balance{};
    double nativeBalance{};
    double bonusBalance{};
    double moneySpent{};
    bool isOutsideWallet{};
    std::string notes = {};

    [[nodiscard]] WalletStruct getWalletStruct() const {
        WalletStruct walletStruct;
        walletStruct.walletId = walletId;
        walletStruct.currencyType = currencyType;
        walletStruct.balance = balance;
        walletStruct.nativeBalance = nativeBalance;
        walletStruct.bonusBalance = bonusBalance;
        walletStruct.moneySpent = moneySpent;
        walletStruct.isOutsideWallet = isOutsideWallet;
        walletStruct.notes = notes;
        return walletStruct;
    }

    [[nodiscard]] CWalletStruct getCWalletStruct() const {
        CWalletStruct walletStruct;
        walletStruct.walletId = walletId;
        stringToCharArray(walletStruct.currencyType, currencyType);
        walletStruct.balance = balance;
        walletStruct.nativeBalance = nativeBalance;
        walletStruct.bonusBalance = bonusBalance;
        walletStruct.moneySpent = moneySpent;
        walletStruct.isOutsideWallet = isOutsideWallet;
        stringToCharArray(walletStruct.notes, notes);
        return walletStruct;
    }

    //! Utility function to serialize a WalletData struct to XML
    [[nodiscard]] std::string serializeToXml() const {
        return serializeToXml(*this);
    }

    //! Utility function to serialize a WalletData struct to XML
    static std::string serializeToXml(const WalletData &wallet) {
        rapidxml::xml_document<> doc;

        auto *root = doc.allocate_node(rapidxml::node_element, "WalletData");
        doc.append_node(root);

        // Helper function to add a new node with a value to the root
        auto addNode = [&](const std::string &nodeName, const std::string &value) {
            auto *node = doc.allocate_node(rapidxml::node_element, nodeName.c_str());
            node->value(doc.allocate_string(value.c_str()));
            root->append_node(node);
        };

        // Add nodes for each field in the WalletData struct
        addNode("walletId", std::to_string(wallet.walletId));
        addNode("currencyType", wallet.currencyType);
        addNode("balance", std::to_string(wallet.balance));
        addNode("nativeBalance", std::to_string(wallet.nativeBalance));
        addNode("bonusBalance", std::to_string(wallet.bonusBalance));
        addNode("moneySpent", std::to_string(wallet.moneySpent));
        addNode("isOutsideWallet", wallet.isOutsideWallet ? "true" : "false");
        addNode("notes", wallet.notes);

        // Convert the document to a string
        std::string xmlString;
        rapidxml::print(std::back_inserter(xmlString), doc, 0);
        return xmlString;
    }

    //! Utility function to deserialize a WalletData struct from XML
    static void deserializeFromXml(const std::string &xml, WalletData &wallet) {
        size_t pos = 0;

        auto getTagValue = [&](const std::string &tag) -> std::string {
            size_t startTag = xml.find("<" + tag + ">", pos);
            if (startTag == std::string::npos) {
                return "";
            }

            size_t endTag = xml.find("</" + tag + ">", startTag);
            if (endTag == std::string::npos) {
                return "";
            }

            pos = endTag + 1;
            return xml.substr(startTag + tag.length() + 2, endTag - startTag - tag.length() - 2);
        };

        wallet.walletId = std::stoi(getTagValue("walletId"));
        wallet.currencyType = getTagValue("currencyType");
        wallet.balance = std::stold(getTagValue("balance"));
        wallet.nativeBalance = std::stold(getTagValue("nativeBalance"));
        wallet.bonusBalance = std::stold(getTagValue("bonusBalance"));
        wallet.moneySpent = std::stold(getTagValue("moneySpent"));
        wallet.isOutsideWallet = (getTagValue("isOutsideWallet") == "true");
        wallet.notes = getTagValue("notes");
    }
};


struct TransactionManagerState {
    bool isBig = false;
    bool hasTxData = false;
    bool hasCardTxData = false;
    bool isReadyFlag = false;
    int txIdCounter = 0;
    char currencies[MAX_WALLETS][MAX_STRING_LENGTH] = {};
    char cardTxTypes[MAX_WALLETS][MAX_STRING_LENGTH] = {};
};


struct BigTransactionMangerState : TransactionManagerState {

    char bigCurrencies[BIG_MAX_WALLETS][MAX_STRING_LENGTH] = {};
    char bigCardTxTypes[BIG_MAX_WALLETS][MAX_STRING_LENGTH] = {};

    BigTransactionMangerState() {
        isBig = true;
    }

};

#endif //NF_TX_CORE_STRUCTS_H