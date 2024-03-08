//
// Created by nfriehs on 11/11/23.
//

#ifndef NF_TX_CORE_ENUMS_H
#define NF_TX_CORE_ENUMS_H

//! Enum for the transaction type
enum TransactionType {
    crypto_purchase,
    supercharger_deposit,
    rewards_platform_deposit_credited,
    supercharger_reward_to_app_credited,
    viban_purchase,
    crypto_earn_program_created,
    crypto_earn_interest_paid,
    supercharger_withdrawal,
    lockup_lock,
    crypto_withdrawal,
    crypto_deposit,
    referral_card_cashback,
    reimbursement,
    card_cashback_reverted,
    crypto_earn_program_withdrawn,
    admin_wallet_credited,
    crypto_wallet_swap_credited,
    crypto_wallet_swap_debited,
    dust_conversion_credited,
    dust_conversion_debited,
    crypto_viban_exchange,
    STRING, //for Card and unknown things
    NONE
};

//! Enum for the different modes
enum Mode {
    CDC,
    Card,
    Default,
    Custom,
    Kraken
};


#endif //NF_TX_CORE_ENUMS_H
