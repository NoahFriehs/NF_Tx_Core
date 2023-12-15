//
// Created by nfriehs on 12/15/23.
//

#include "Util.h"
#include "../FileLog.h"
#include <iomanip>
#include <algorithm>

TransactionType ttConverter(const std::string &s) {
    std::string lowercase = s;
    // Convert the input string to lowercase for case-insensitive comparison
    std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(), ::tolower);

    if (lowercase == "crypto_purchase") return crypto_purchase;
    if (lowercase == "supercharger_deposit") return supercharger_deposit;
    if (lowercase == "rewards_platform_deposit_credited") return rewards_platform_deposit_credited;
    if (lowercase == "supercharger_reward_to_app_credited") return supercharger_reward_to_app_credited;
    if (lowercase == "viban_purchase") return viban_purchase;
    if (lowercase == "crypto_earn_program_created") return crypto_earn_program_created;
    if (lowercase == "crypto_earn_interest_paid") return crypto_earn_interest_paid;
    if (lowercase == "supercharger_withdrawal") return supercharger_withdrawal;
    if (lowercase == "lockup_lock") return lockup_lock;
    if (lowercase == "crypto_withdrawal") return crypto_withdrawal;
    if (lowercase == "crypto_deposit") return crypto_deposit;
    if (lowercase == "referral_card_cashback") return referral_card_cashback;
    if (lowercase == "reimbursement") return reimbursement;
    if (lowercase == "card_cashback_reverted") return card_cashback_reverted;
    if (lowercase == "crypto_earn_program_withdrawn") return crypto_earn_program_withdrawn;
    if (lowercase == "admin_wallet_credited") return admin_wallet_credited;
    if (lowercase == "crypto_wallet_swap_credited") return crypto_wallet_swap_credited;
    if (lowercase == "crypto_wallet_swap_debited") return crypto_wallet_swap_debited;
    if (lowercase == "dust_conversion_credited") return dust_conversion_credited;
    if (lowercase == "dust_conversion_debited") return dust_conversion_debited;
    if (lowercase == "crypto_viban_exchange") return crypto_viban_exchange;

    return STRING;
}

std::vector<std::string> splitString(const std::string &input, char delimiter) {
    std::vector<std::string> result;
    std::istringstream stream(input);
    std::string token;

    while (std::getline(stream, token, delimiter)) {
        result.push_back(token);
    }

    return result;
}


std::tm TimestampConverter::stringToTm(const std::string &timestamp_str) {
    std::tm timestamp_tm = {};
    if (timestamp_str.empty()) {
        FileLog::w("TimestampConverter", "Empty timestamp string.");
        return timestamp_tm;
    }
    std::istringstream ss(timestamp_str);
    ss >> std::get_time(&timestamp_tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        throw std::runtime_error("Failed to parse timestamp.");
    }
    return timestamp_tm;
}

std::string TimestampConverter::tmToString(const std::tm &timestamp_tm) {
    std::ostringstream ss;
    ss << std::put_time(&timestamp_tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}