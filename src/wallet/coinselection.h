// Copyright (c) 2017-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_COINSELECTION_H
#define BITCOIN_WALLET_COINSELECTION_H

#include <amount.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <random.h>

//! target minimum change amount
static constexpr CAmount MIN_CHANGE{COIN / 100};
//! final minimum change amount after paying for fees
static const CAmount MIN_FINAL_CHANGE = MIN_CHANGE/2;

class CInputCoin {
public:
    CInputCoin(const CTransactionRef& tx, unsigned int i)
    {
        if (!tx)
            throw std::invalid_argument("tx should not be null");
        if (i >= tx->vout.size())
            throw std::out_of_range("The output index is out of range");

        outpoint = COutPoint(tx->GetHash(), i);
        txout = tx->vout[i];
        effective_value = txout.nValue;
    }

    CInputCoin(const CTransactionRef& tx, unsigned int i, int input_bytes) : CInputCoin(tx, i)
    {
        m_input_bytes = input_bytes;
    }

    COutPoint outpoint;
    CTxOut txout;
    CAmount effective_value;
    CAmount m_fee{0};
    CAmount m_long_term_fee{0};

    /** Pre-computed estimated size of this output as a fully-signed input in a transaction. Can be -1 if it could not be calculated */
    int m_input_bytes{-1};

    bool operator<(const CInputCoin& rhs) const {
        return outpoint < rhs.outpoint;
    }

    bool operator!=(const CInputCoin& rhs) const {
        return outpoint != rhs.outpoint;
    }

    bool operator==(const CInputCoin& rhs) const {
        return outpoint == rhs.outpoint;
    }
};

struct CoinSelectionParams
{
    size_t change_output_size = 0;
    size_t change_spend_size = 0;
    CFeeRate m_effective_feerate;
    CFeeRate m_long_term_feerate;
    CFeeRate m_discard_feerate;
    size_t tx_noinputs_size = 0;
    //! Indicate that we are subtracting the fee from outputs
    bool m_subtract_fee_outputs = false;
    bool m_avoid_partial_spends = false;

    CoinSelectionParams(size_t change_output_size, size_t change_spend_size, CFeeRate effective_feerate,
                        CFeeRate long_term_feerate, CFeeRate discard_feerate, size_t tx_noinputs_size, bool avoid_partial) :
        change_output_size(change_output_size),
        change_spend_size(change_spend_size),
        m_effective_feerate(effective_feerate),
        m_long_term_feerate(long_term_feerate),
        m_discard_feerate(discard_feerate),
        tx_noinputs_size(tx_noinputs_size),
        m_avoid_partial_spends(avoid_partial)
    {}
    CoinSelectionParams() {}
};

struct CoinEligibilityFilter
{
    const int conf_mine;
    const int conf_theirs;
    const uint64_t max_ancestors;
    const uint64_t max_descendants;
    const bool m_include_partial_groups{false}; //! Include partial destination groups when avoid_reuse and there are full groups

    CoinEligibilityFilter(int conf_mine, int conf_theirs, uint64_t max_ancestors) : conf_mine(conf_mine), conf_theirs(conf_theirs), max_ancestors(max_ancestors), max_descendants(max_ancestors) {}
    CoinEligibilityFilter(int conf_mine, int conf_theirs, uint64_t max_ancestors, uint64_t max_descendants) : conf_mine(conf_mine), conf_theirs(conf_theirs), max_ancestors(max_ancestors), max_descendants(max_descendants) {}
    CoinEligibilityFilter(int conf_mine, int conf_theirs, uint64_t max_ancestors, uint64_t max_descendants, bool include_partial) : conf_mine(conf_mine), conf_theirs(conf_theirs), max_ancestors(max_ancestors), max_descendants(max_descendants), m_include_partial_groups(include_partial) {}
};

struct OutputGroup
{
    std::vector<CInputCoin> m_outputs;
    bool m_from_me{true};
    CAmount m_value{0};
    int m_depth{999};
    size_t m_ancestors{0};
    size_t m_descendants{0};
    CAmount effective_value{0};
    CAmount fee{0};
    CAmount long_term_fee{0};
    CoinSelectionParams m_cs_params;

    OutputGroup() {}
    OutputGroup(const CoinSelectionParams& params) :
        m_cs_params(params)
    {}

    void Insert(const CInputCoin& output, int depth, bool from_me, size_t ancestors, size_t descendants, bool positive_only);
    bool EligibleForSpending(const CoinEligibilityFilter& eligibility_filter) const;
    CAmount GetSelectionAmount() const;
};

bool SelectCoinsBnB(std::vector<OutputGroup>& utxo_pool, const CAmount& actual_target, const CAmount& cost_of_change, std::set<CInputCoin>& out_set, CAmount& value_ret);

// Original coin selection algorithm as a fallback
bool KnapsackSolver(const CAmount& nTargetValue, std::vector<OutputGroup>& groups, std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet);

#endif // BITCOIN_WALLET_COINSELECTION_H
