// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/coinselection.h>
#include <util.h>
#include <utilmoneystr.h>

// Descending order comparator
struct {
    bool operator()(const CInputCoin& a, const CInputCoin& b) const
    {
        return a.effective_value > b.effective_value;
    }
} descending;

/*
 * This is the Branch and Bound Coin Selection algorithm designed by Murch. It searches for an input set that can pay for the
 * spending target and does not exceed the spending target by more than the cost of creating and spending a change output. The
 * algorithm uses a depth-first search on a binary tree. In the binary tree, each node corresponds to the inclusion or the omission
 * of a UTXO. UTXOs are sorted by their effective values and the trees is explored deterministically per the inclusion branch first.
 * At each node, the algorithm checks whether the selection is within the target range. While the selection has not reached the
 * target range, more UTXOs are included. When a selection's value exceeds the target range, the complete subtree deriving from
 * this selection can be omitted. At that point, the last included UTXO is deselected and the corresponding omission branch explored
 * instead. The search ends after the complete tree has been searched or after a limited number of tries.
 *
 * The search continues to search for better solutions after one solution has been found. The best solution is chosen by minimizing
 * the waste metric. The waste metric is defined as the cost to spend the current inputs at the given fee rate minus the long
 * term expected cost to spend the inputs, plus the amount the selection exceeds the spending target:
 *
 * waste = selectionTotal - target + inputs × (currentFeeRate - longTermFeeRate)
 *
 * The algorithm uses two additional optimizations. A lookahead keeps track of the total value of the unexplored UTXOs. A subtree
 * is not explored if the lookahead indicates that the target range cannot be reached. Further, it is unnecessary to test
 * equivalent combinations. This allows us to skip testing the inclusion of UTXOs that match the effective value and waste of an
 * omitted predecessor.
 *
 * The Branch and Bound algorithm is described in detail in Murch's Master Thesis: https://murch.one/wp-content/uploads/2016/11/erhardt2016coinselection.pdf
 *
 * @param const std::vector<CInputCoin>& utxo_pool -> The set of UTXOs that we are choosing from. These UTXOs will be sorted in descending order
 *                                             by effective value and the CInputCoins' values are their effective values.
 * @param const CAmount& target_value -> This is the value that we want to select. It is the lower bound of the range.
 * @param const CAmount& cost_of_change -> This is the cost of creating and spending a change output. This plus target_value is the upper bound
 *                                  of the range.
 * @param std::set<CInputCoin>& out_set -> This is an output parameter for the set of CInputCoins that have been selected.
 * @param CAmount& value_ret -> This is an output parameter for the total value of the CInputCoins that were selected.
 * @param CAmount not_input_fees -> The fees that need to be paid for the outputs and fixed size overhead (version, locktime, marker and flag)
 */

static const size_t TOTAL_TRIES = 100000;

bool SelectCoinsBnB(std::vector<CInputCoin>& utxo_pool, const CAmount& target_value, const CAmount& cost_of_change, std::set<CInputCoin>& out_set, CAmount& value_ret, CAmount not_input_fees)
{
    out_set.clear();
    CAmount curr_value = 0;

    int depth = 0;
    std::vector<bool> curr_selection(utxo_pool.size()); // select the utxo at this index
    bool backtrack = false;
    CAmount actual_target = not_input_fees + target_value;

    // Calculate curr_available_value
    CAmount curr_available_value = 0;
    for (const CInputCoin& utxo : utxo_pool) {
        curr_available_value += utxo.effective_value;
    }
    if (curr_available_value < actual_target) {
        return false;
    }

    // Sort the utxo_pool
    std::sort(utxo_pool.begin(), utxo_pool.end(), descending);

    CAmount curr_waste = 0;
    std::vector<bool> best_selection;
    CAmount best_waste = MAX_MONEY;

    // Depth First search loop for choosing the UTXOs
    for (size_t i = 0; i < TOTAL_TRIES; ++i) {
        // Conditions for starting a backtrack
        if (curr_value + curr_available_value < actual_target ||                // Cannot possibly reach target with the amount remaining in the curr_available_value.
            curr_value > actual_target + cost_of_change ||    // Selected value is out of range, go back and try other branch
            (curr_waste > best_waste && (utxo_pool.at(0).fee - utxo_pool.at(0).long_term_fee) > 0)) { // Don't select things which we know will be more wasteful if the waste is increasing
            backtrack = true;
        } else if (curr_value >= actual_target) {       // Selected value is within range
            curr_waste += (curr_value - actual_target); // This is the excess value which is added to the waste for the below comparison
            // Adding another UTXO after this check could bring the waste down if the long term fee is higher than the current fee.
            // However we are not going to explore that because this optimization for the waste is only done when we have hit our target
            // value. Adding any more UTXOs will be just burning the UTXO; it will go entirely to fees. Thus we aren't going to
            // explore any more UTXOs to avoid burning money like that.
            if (curr_waste <= best_waste) {
                best_selection.assign(curr_selection.begin(), curr_selection.end());
                best_waste = curr_waste;
            }
            curr_waste -= (curr_value - actual_target); // Remove the excess value as we will be selecting different coins now
            backtrack = true;
        }

        // Backtracking, moving backwards
        if (backtrack) {
            backtrack = false; // Reset
            --depth;

            // Walk backwards to find the last included UTXO that still needs to have its omission branch traversed.
            while (depth >= 0 && !curr_selection.at(depth)) {
                // Step back one
                curr_available_value += utxo_pool.at(depth).effective_value;
                --depth;
            }
            if (depth < 0) { // We have walked back to the first utxo and no branch is untraversed. All solutions searched
                break;
            }

            // These were always included first, try excluding now
            curr_selection.at(depth) = false;
            curr_value -= utxo_pool.at(depth).effective_value;
            curr_waste -= (utxo_pool.at(depth).fee - utxo_pool.at(depth).long_term_fee);
            ++depth;
        } else { // Moving forwards, continuing down this branch
            // Assert that this utxo is not negative. It should never be negative, effective value calculation should have removed it
            assert(utxo_pool.at(depth).effective_value > 0);

            // Avoid searching a branch if the previous UTXO has the same value and same waste and was excluded. Since the ratio of fee to
            // long term fee is the same, we only need to check if one of those values match in order to know that the waste is the same.
            if (depth > 0 && !curr_selection.at(depth - 1) &&
                utxo_pool.at(depth).effective_value == utxo_pool.at(depth - 1).effective_value &&
                utxo_pool.at(depth).fee == utxo_pool.at(depth - 1).fee) {
                curr_selection.at(depth) = false;
                curr_available_value -= utxo_pool.at(depth).effective_value;
                ++depth;
            } else {
                // Remove this utxo from the curr_available_value utxo amount
                curr_available_value -= utxo_pool.at(depth).effective_value;
                // Increase waste
                curr_waste += (utxo_pool.at(depth).fee - utxo_pool.at(depth).long_term_fee);
                // Inclusion branch first (Largest First Exploration)
                curr_selection.at(depth) = true;
                curr_value += utxo_pool.at(depth).effective_value;
                ++depth;
            }
        }
    }

    // Check for solution
    if (best_selection.empty()) {
        return false;
    }

    // Set output set
    value_ret = 0;
    for (size_t i = 0; i < best_selection.size(); ++i) {
        if (best_selection.at(i)) {
            out_set.insert(utxo_pool.at(i));
            value_ret += utxo_pool.at(i).txout.nValue;
        }
    }

    return true;
}

static void ApproximateBestSubset(const std::vector<CInputCoin>& vValue, const CAmount& nTotalLower, const CAmount& nTargetValue,
                                  std::vector<char>& vfBest, CAmount& nBest, int iterations = 1000)
{
    std::vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    FastRandomContext insecure_rand;

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        CAmount nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng is fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? insecure_rand.randbool() : !vfIncluded[i])
                {
                    nTotal += vValue[i].txout.nValue;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].txout.nValue;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
}

bool KnapsackSolver(std::vector<CInputCoin>& utxo_pool, const CAmount& nTargetValue, std::set<CInputCoin>& out_set, CAmount& value_ret)
{
    out_set.clear();
    value_ret = 0;

    // List of values less than target
    boost::optional<CInputCoin> coinLowestLarger;
    std::vector<CInputCoin> vValue;
    CAmount nTotalLower = 0;

    random_shuffle(utxo_pool.begin(), utxo_pool.end(), GetRandInt);

    for (const CInputCoin coin : utxo_pool)
    {
        if (coin.txout.nValue == nTargetValue)
        {
            out_set.insert(coin);
            value_ret += coin.txout.nValue;
            return true;
        }
        else if (coin.txout.nValue < nTargetValue + MIN_CHANGE)
        {
            vValue.push_back(coin);
            nTotalLower += coin.txout.nValue;
        }
        else if (!coinLowestLarger || coin.txout.nValue < coinLowestLarger->txout.nValue)
        {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue)
    {
        for (const auto& input : vValue)
        {
            out_set.insert(input);
            value_ret += input.txout.nValue;
        }
        return true;
    }

    if (nTotalLower < nTargetValue)
    {
        if (!coinLowestLarger)
            return false;
        out_set.insert(coinLowestLarger.get());
        value_ret += coinLowestLarger->txout.nValue;
        return true;
    }

    // Solve subset sum by stochastic approximation
    std::sort(vValue.begin(), vValue.end(), descending);
    std::vector<char> vfBest;
    CAmount nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + MIN_CHANGE)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + MIN_CHANGE, vfBest, nBest);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger &&
        ((nBest != nTargetValue && nBest < nTargetValue + MIN_CHANGE) || coinLowestLarger->txout.nValue <= nBest))
    {
        out_set.insert(coinLowestLarger.get());
        value_ret += coinLowestLarger->txout.nValue;
    }
    else {
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
            {
                out_set.insert(vValue[i]);
                value_ret += vValue[i].txout.nValue;
            }

        if (LogAcceptCategory(BCLog::SELECTCOINS)) {
            LogPrint(BCLog::SELECTCOINS, "SelectCoins() best subset: ");
            for (unsigned int i = 0; i < vValue.size(); i++) {
                if (vfBest[i]) {
                    LogPrint(BCLog::SELECTCOINS, "%s ", FormatMoney(vValue[i].txout.nValue));
                }
            }
            LogPrint(BCLog::SELECTCOINS, "total %s\n", FormatMoney(nBest));
        }
    }

    return true;
}
