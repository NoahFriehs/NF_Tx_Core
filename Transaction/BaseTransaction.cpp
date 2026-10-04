
#include "BaseTransaction.h"
#include "../Util/Util.h"

int txIdCounter;    //TODO: be careful with this when loading from DB

BaseTransaction::BaseTransaction() = default;

BaseTransaction::~BaseTransaction() = default;

void BaseTransaction::parseCDC(const std::string &txString) {
    auto tx = splitCsvLine(txString, ',');

    // Guard the unbounded column accesses below (operator[] does not check).
    if (tx.size() < 10) {
        throw std::invalid_argument(
                "CDC line needs at least 10 columns, got " + std::to_string(tx.size()));
    }
    transactionId = txIdCounter++;
    transactionDate = TimestampConverter::stringToTm(tx[0]);
    description = tx[1];
    currencyType = tx[2];
    amount = std::stold(tx[3]);
    nativeAmount = std::stold(tx[7]);
    transactionTypeString = tx[9];
    transactionType = ttConverter(transactionTypeString);

    if (tx.size() == 11) transactionHash = tx[10];
    if (transactionType == viban_purchase) {
        toCurrencyType = tx[4];
        toAmount = std::stold(tx[5]);
    }

}

std::string BaseTransaction::getCurrencyType() const {
    return currencyType;
}


long double BaseTransaction::getAmount() const {
    return amount;
}

long double BaseTransaction::getNativeAmount() const {
    return nativeAmount;
}

TransactionType BaseTransaction::getTransactionType() const {
    return transactionType;
}

void BaseTransaction::setAmountToAmountBonus() {
    amountBonus = amount;
}

void BaseTransaction::setAmount(long double amount_) {
    amount = amount_;
}

void BaseTransaction::setNativeAmount(long double nativeAmount_) {
    nativeAmount = nativeAmount_;
}

void BaseTransaction::setCurrencyType(const std::string &currencyType_) {
    currencyType = currencyType_;
}

void BaseTransaction::setToAmount(long double toAmount_) {
    toAmount = toAmount_;
}

void BaseTransaction::setWalletId(int id) {
    walletId = id;
}

void BaseTransaction::setFromWalletId(int id) {
    fromWalletId = id;
}

std::string BaseTransaction::getToCurrencyType() const {
    return toCurrencyType;
}

std::string BaseTransaction::getTransactionTypeString() const {
    return transactionTypeString;
}

int BaseTransaction::getWalletId() const {
    return walletId;
}

long double BaseTransaction::getAmountBonus() const {
    return amountBonus;
}

long double BaseTransaction::getFeeAmount() const {
    return feeAmount;
}

std::string BaseTransaction::getFeeAsset() const {
    return feeAsset;
}

long double BaseTransaction::getToAmount() const {
    return toAmount;
}

TransactionData BaseTransaction::getTransactionData() const {
    TransactionData txData;
    txData.transactionId = transactionId;
    txData.walletId = walletId;
    txData.fromWalletId = fromWalletId;
    txData.description = description;
    txData.transactionDate = transactionDate;
    txData.currencyType = currencyType;
    txData.toCurrencyType = toCurrencyType;
    txData.amount = amount;
    txData.toAmount = toAmount;
    txData.nativeAmount = nativeAmount;
    txData.amountBonus = amountBonus;
    txData.transactionTypeOrdinal = transactionType;
    txData.transactionHash = transactionHash;
    txData.isOutsideTransaction = isOutsideTransaction;
    txData.notes = notes;

    return txData;
}

void BaseTransaction::parseCard(const std::string &txString) {
    auto tx = splitCsvLine(txString, ',');

    // Guard the unbounded column accesses below (operator[] does not check).
    if (tx.size() < 8) {
        throw std::invalid_argument(
                "Card line needs at least 8 columns, got " + std::to_string(tx.size()));
    }
    transactionId = txIdCounter++;
    transactionDate = TimestampConverter::stringToTm(tx[0]);
    description = tx[1];
    currencyType = tx[2];
    amount = std::stold(tx[3]);
    nativeAmount = std::stold(tx[7]);
    transactionTypeString = tx[1];
    transactionType = STRING;

}

TransactionStruct BaseTransaction::getTransactionStruct() {
    TransactionStruct data;
    data.transactionId = transactionId;
    data.walletId = walletId;
    data.fromWalletId = fromWalletId;
    data.description = description;
    data.transactionDate = transactionDate;
    data.currencyType = currencyType;
    data.toCurrencyType = toCurrencyType;
    data.feeAsset = feeAsset;
    data.amount = amount;
    data.toAmount = toAmount;
    data.nativeAmount = nativeAmount;
    data.amountBonus = amountBonus;
    data.transactionType = transactionType;
    data.transactionTypeString = transactionTypeString;
    data.transactionHash = transactionHash;
    data.isOutsideTransaction = isOutsideTransaction;
    data.notes = notes;
    return data;
}

void BaseTransaction::fromTransactionStruct(const TransactionStruct &data) {
    transactionId = data.transactionId;
    walletId = data.walletId;
    fromWalletId = data.fromWalletId;
    description = data.description;
    transactionDate = data.transactionDate;
    currencyType = data.currencyType;
    toCurrencyType = data.toCurrencyType;
    feeAsset = data.feeAsset;
    amount = data.amount;
    toAmount = data.toAmount;
    nativeAmount = data.nativeAmount;
    amountBonus = data.amountBonus;
    transactionType = data.transactionType;
    transactionTypeString = data.transactionTypeString;
    transactionHash = data.transactionHash;
    isOutsideTransaction = data.isOutsideTransaction;
    notes = data.notes;
}

void BaseTransaction::setTxIdCounter(int txIdCounter_) {
    // Forward-only: the counter may only increase so that transaction ids are
    // never reused. Values at or behind the current counter are ignored
    // (stale saved state) instead of throwing.
    if (txIdCounter_ > txIdCounter) {
        txIdCounter = txIdCounter_;
    } else if (txIdCounter_ < txIdCounter) {
        // Equal values are the normal "restore my own state" case - no log.
        FileLog::w("BaseTransaction",
                   "Ignoring txIdCounter " + std::to_string(txIdCounter_) +
                           " (current: " + std::to_string(txIdCounter) + ")");
    }
}

int BaseTransaction::getTxIdCounter() {
    return txIdCounter;
}

int BaseTransaction::getTransactionId() const {
    return transactionId;
}

void BaseTransaction::setTransactionData(const TransactionStruct &txStruct) {
    transactionId = txStruct.transactionId;
    walletId = txStruct.walletId;
    fromWalletId = txStruct.fromWalletId;
    description = txStruct.description;
    transactionDate = txStruct.transactionDate;
    currencyType = txStruct.currencyType;
    toCurrencyType = txStruct.toCurrencyType;
    feeAsset = txStruct.feeAsset;
    amount = txStruct.amount;
    toAmount = txStruct.toAmount;
    nativeAmount = txStruct.nativeAmount;
    amountBonus = txStruct.amountBonus;
    transactionType = txStruct.transactionType;
    transactionTypeString = txStruct.transactionTypeString;
    transactionHash = txStruct.transactionHash;
    isOutsideTransaction = txStruct.isOutsideTransaction;
    notes = txStruct.notes;
}

void BaseTransaction::setTransactionTypeString(const std::string &transactionTypeStringToSet) {
    //BaseTransaction::transactionTypeString = transactionTypeStringToSet;
}

void BaseTransaction::parseKraken(const std::string &txString) {
    //"txid","ordertxid","pair","time","type","ordertype","price","cost","fee","vol","margin","misc","ledgers"
    //"T67CDX-SB6EI-XIRITS","O3VT22-PENXL-5BRYNG","XXBTZEUR","2023-06-19 13:34:05.4856","buy","limit",24300.00000,49.99992,0.13000,0.00205761,0.00000,"initiated","LUBMNQ-ZAVX6-IGKZMJ,LWLY4J-OZSCV-P4RHND"
    // Quote-aware split: the ledgers field contains commas.
    auto tx = splitCsvLine(txString, ',');

    // Guard the unbounded column accesses below (operator[] does not check).
    if (tx.size() < 12) {
        throw std::invalid_argument(
                "Kraken line needs at least 12 columns, got " + std::to_string(tx.size()));
    }
    transactionId = txIdCounter++;
    transactionDate = TimestampConverter::stringToTm(tx[3]);
    description = tx[2] + "   " + tx[11];
    transactionTypeString = tx[4];
    if (tx[4] == "buy") {
        currencyType = getKrakenCurrencyType(tx[2].substr(0, 4));   //only if type == buy
        amount = std::stold(tx[9]);
        nativeAmount = std::stold(tx[7]);
        transactionType = crypto_purchase;
    } else {
        currencyType = getKrakenCurrencyType(tx[2].substr(4, 7));   //only if type == sell and untested
        amount = -std::stold(tx[9]);
        nativeAmount = -std::stold(tx[7]);
        transactionType = STRING;
    }
    feeAmount = std::stold(tx[8]);

}

void BaseTransaction::parseBlockPit(const std::string &txString) {
    // BlockPit CSV uses semicolons as delimiter with this format:
    // Date (UTC);Integration Name;Label;Outgoing Asset;Outgoing Amount;
    // Incoming Asset;Incoming Amount;Fee Asset (optional);Fee Amount (optional);
    // Comment (optional);Trx. ID (optional);Source Type;Source Name
    auto tx = splitCsvLine(txString, ';');

    // Guard the unbounded column accesses below (operator[] does not check).
    if (tx.size() < 7) {
        throw std::invalid_argument(
                "BlockPit line needs at least 7 columns, got " + std::to_string(tx.size()));
    }
    transactionId = txIdCounter++;
    transactionDate = TimestampConverter::stringToTmBlockPit(tx[0]);
    description = tx[1];   // Integration Name

    // Column 2: Label → determines transaction type
    std::string label = tx[2];

    // Columns 3-4: Outgoing (what leaves the wallet)
    std::string outgoingAsset = tx[3];
    std::string outgoingAmountStr = tx[4];

    // Columns 5-6: Incoming (what arrives in the wallet)
    std::string incomingAsset = tx[5];
    std::string incomingAmountStr = tx[6];

    // Column 7: Fee Asset (optional)
    std::string incomingFeeAsset = tx.size() > 7 ? tx[7] : "";
    // Column 8: Fee Amount (optional)
    std::string incomingFeeAmountStr = tx.size() > 8 ? tx[8] : "";
    // Column 9: Comment (optional)
    std::string comment = tx.size() > 9 ? tx[9] : "";
    // Column 10: Transaction ID (optional)
    transactionHash = tx.size() > 10 ? tx[10] : "";
    // Column 12: Source Name (optional) — stored in notes
    std::string sourceName = tx.size() > 12 ? tx[12] : "";

    // Store metadata in notes
    notes = "Source: " + sourceName;
    if (!comment.empty()) {
        notes += " | Comment: " + comment;
    }

    // Parse fee fields
    if (!incomingFeeAsset.empty()) {
        feeAsset = incomingFeeAsset;
    }
    if (!incomingFeeAmountStr.empty()) {
        feeAmount = std::stold(incomingFeeAmountStr);
    }

    // Signed-amount convention (same as the CDC/Kraken parsers): a positive
    // amount increases the owning wallet's balance, a negative amount reduces
    // it. BlockPit reports absolute values, so outgoing quantities are stored
    // negated. Wallets are never mutated here — only amounts are prepared.
    auto toAmountValue = [](const std::string &s) -> long double {
        return s.empty() ? 0.0L : std::stold(s);
    };

    // Determine transaction type based on Label
    if (label == "Interest" || label == "Staking") {
        transactionTypeString = "crypto_earn_interest_paid";
        transactionType = crypto_earn_interest_paid;
        // Only incoming — wallet is the incoming asset
        currencyType = incomingAsset;
        amount = toAmountValue(incomingAmountStr);
    } else if (label == "Airdrop") {
        transactionTypeString = "airdrop";
        transactionType = crypto_airdrop_credited;
        currencyType = incomingAsset;
        amount = toAmountValue(incomingAmountStr);
    } else if (label == "Deposit") {
        transactionTypeString = "crypto_deposit";
        transactionType = crypto_deposit;
        currencyType = incomingAsset;
        amount = toAmountValue(incomingAmountStr);
    } else if (label == "Non-Taxable In") {
        transactionTypeString = "crypto_transfer";
        transactionType = crypto_transfer;
        currencyType = incomingAsset;
        amount = toAmountValue(incomingAmountStr);
    } else if (label == "Withdrawal") {
        transactionTypeString = "crypto_withdrawal";
        transactionType = crypto_withdrawal;
        // Only outgoing — wallet is the outgoing asset, amount is negative
        currencyType = outgoingAsset;
        amount = -toAmountValue(outgoingAmountStr);
    } else if (label == "Trade") {
        bool hasOutgoing = !outgoingAsset.empty() && !outgoingAmountStr.empty();
        bool hasIncoming = !incomingAsset.empty() && !incomingAmountStr.empty();

        if (hasOutgoing && hasIncoming) {
            if (isFiatCurrency(outgoingAsset)) {
                // Classic purchase: currencyType = incoming crypto, amount = +qty.
                // nativeAmount = fiat cost, so the crypto wallet's moneySpent is
                // tracked exactly like in the CDC parser.
                currencyType = incomingAsset;
                amount = toAmountValue(incomingAmountStr);
                toCurrencyType = outgoingAsset;
                toAmount = toAmountValue(outgoingAmountStr);
                nativeAmount = toAmount;
                transactionTypeString = "crypto_purchase";
                transactionType = crypto_purchase;
            } else if (isFiatCurrency(incomingAsset)) {
                // Sale: the opposite of the purchase above. The crypto wallet is
                // debited (negative amount) and the fiat proceeds land in the
                // outside fiat wallet — mirroring the CDC/Kraken convention of
                // recording a sale as a negative crypto_purchase. A negative
                // nativeAmount reduces the crypto wallet's moneySpent.
                currencyType = outgoingAsset;
                amount = -toAmountValue(outgoingAmountStr);
                toCurrencyType = incomingAsset;
                toAmount = toAmountValue(incomingAmountStr);
                nativeAmount = -toAmount;
                transactionTypeString = "crypto_purchase";
                transactionType = crypto_purchase;
            } else {
                // Crypto-to-crypto swap: +incoming here, -outgoing is applied by
                // the manager to the outgoing asset's wallet.
                currencyType = incomingAsset;
                amount = toAmountValue(incomingAmountStr);
                toCurrencyType = outgoingAsset;
                toAmount = toAmountValue(outgoingAmountStr);
                transactionTypeString = "Swap";
                transactionType = STRING;
            }
        } else if (hasIncoming) {
            // Degenerate Trade (incoming only) — plain credit
            currencyType = incomingAsset;
            amount = toAmountValue(incomingAmountStr);
            transactionTypeString = "Transfer In";
            transactionType = STRING;
        } else if (hasOutgoing) {
            // Degenerate Trade (outgoing only) — plain debit
            currencyType = outgoingAsset;
            amount = -toAmountValue(outgoingAmountStr);
            transactionTypeString = "Transfer Out";
            transactionType = STRING;
        }
    } else if (label == "Bounty" || label == "Cashback") {
        transactionTypeString = "bounty";
        transactionType = crypto_bounty_credited;
        currencyType = incomingAsset;
        amount = toAmountValue(incomingAmountStr);
    } else if (label == "Gift Received") {
        transactionTypeString = "gift received";
        transactionType = crypto_gift_received;
        currencyType = incomingAsset;
        amount = toAmountValue(incomingAmountStr);
    } else if (label == "Fee" || label == "Lost" || label == "Non-Taxable Out") {
        // Outgoing-only labels — the owning wallet's balance is reduced
        if (label == "Non-Taxable Out") {
            transactionTypeString = "crypto_transfer";
            transactionType = crypto_transfer;
        } else {
            transactionTypeString = label;
            transactionType = STRING;
        }
        currencyType = outgoingAsset;
        amount = -toAmountValue(outgoingAmountStr);
    } else {
        // Unknown label — fall back to STRING and route the amount by direction
        transactionTypeString = label;
        transactionType = STRING;
        if (!outgoingAsset.empty()) {
            currencyType = outgoingAsset;
            amount = -toAmountValue(outgoingAmountStr);
        } else if (!incomingAsset.empty()) {
            currencyType = incomingAsset;
            amount = toAmountValue(incomingAmountStr);
        }
    }

    // Handle edge cases where currencyType ended up empty
    if (currencyType.empty() && !incomingAsset.empty()) {
        currencyType = incomingAsset;
    }
}

//! Check if a currency string represents a fiat currency (EUR, USD, etc.)
bool BaseTransaction::isFiatCurrency(const std::string &currency) {
    static const std::vector<std::string> fiatCurrencies = {
        "EUR", "USD", "GBP", "CHF", "JPY", "CNY", "CAD", "AUD", "NZD"
    };
    for (const auto &fiat: fiatCurrencies) {
        if (currency == fiat) return true;
    }
    return false;
}
