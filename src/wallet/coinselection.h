// Copyright (c) 2017-2018 The Bitcoin Core developers
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
    CFeeRate m_effective_feerate{0};
    CAmount long_term_fee{0};
    CFeeRate m_long_term_feerate{0};

    OutputGroup() {}
    OutputGroup(const CFeeRate& effective_feerate, const CFeeRate& long_term_feerate) :
        m_effective_feerate(effective_feerate),
        m_long_term_feerate(long_term_feerate)
    {}

    void Insert(const CInputCoin& output, int depth, bool from_me, size_t ancestors, size_t descendants, bool positive_only);
    bool EligibleForSpending(const CoinEligibilityFilter& eligibility_filter) const;
};

struct SelectionResult
{
    /** Set of inputs selected by the algorithm to use in the transaction */
    std::set<CInputCoin> selected_inputs;
    /** Amount of fees that cover all of the inputs.
     *  Because we include the fees for transaction overhead and outputs in the
     *  selection target, we are unable to account for those here.
     *  This is not a function because the fees we pay may differ from the fee set
     *  in the CInputCoins as some algorithms will overpay the fee a little bit to
     *  hit a specific target.
     */
    CAmount input_fees{0};

    /** Get the sum of the input values */
    CAmount GetSelectedValue() const;
    /** Check if this selection is equivalent to another one. Equivalent means same input values, but maybe different inputs (i.e. same value, different prevout) */
    bool EquivalentResult(const SelectionResult& other) const;
    /** Check if this selection is equal to another one. Equal means same inputs (i.e same value and prevout) */
    bool EqualResult(const SelectionResult& other) const;

    void Clear();

    void AddInput(const OutputGroup& group);
};

bool SelectCoinsBnB(std::vector<OutputGroup>& utxo_pool, const CAmount& actual_target, const CAmount& cost_of_change, SelectionResult& result);

bool SelectCoinsSRD(std::vector<OutputGroup>& utxo_pool, const CAmount& target_value, std::set<CInputCoin>& out_set, CAmount& value_ret);

// Original coin selection algorithm as a fallback
bool KnapsackSolver(const CAmount& nTargetValue, std::vector<OutputGroup>& groups, std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet, SelectionResult& result);

#endif // BITCOIN_WALLET_COINSELECTION_H
