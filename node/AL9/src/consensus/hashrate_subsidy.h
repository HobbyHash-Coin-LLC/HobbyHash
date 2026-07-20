// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#ifndef BITCOIN_CONSENSUS_HASHRATE_SUBSIDY_H
#define BITCOIN_CONSENSUS_HASHRATE_SUBSIDY_H

#include <consensus/amount.h>
#include <consensus/params.h>

#include <optional>

class CBlockIndex;

namespace Consensus {

double HashrateHpsToPh(double hps);

/** Subsidy from network average PH/s using the V4 tier scale. */
CAmount HashrateSubsidyFromPh(double network_ph);

/**
 * Estimate network HPS from chain history ending at pindexPrev.
 * Returns nullopt when wall span is below nSubsidyHashrateWindowSeconds (use fallback subsidy).
 */
std::optional<double> EstimateNetworkHashPS(const CBlockIndex* pindexPrev, const Params& params);

/** Post-fork subsidy using rolling hash window; legacy path when height < activation. */
CAmount GetBlockSubsidy(int nHeight, const Params& consensusParams, const CBlockIndex* pindexPrev = nullptr);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_HASHRATE_SUBSIDY_H
