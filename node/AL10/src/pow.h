// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/dual_pow.h>
#include <consensus/params.h>

#include <cstdint>

int64_t PowTargetSpacingAtHeight(const Consensus::Params& params, int64_t height);

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

/**
 * Convert nBits value to target.
 *
 * @param[in] nBits     compact representation of the target
 * @param[in] pow_limit PoW limit (consensus parameter)
 *
 * @return              the proof-of-work target or nullopt if the nBits value
 *                      is invalid (due to overflow or exceeding pow_limit)
 */
std::optional<arith_uint256> DeriveTarget(unsigned int nBits, uint256 pow_limit);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);

/**
 * Return false if the proof-of-work requirement specified by new_nbits at a
 * given height is not possible, given the proof-of-work on the prior block as
 * specified by old_nbits.
 *
 * This function only checks that the new value is within the permitted per-block
 * adjustment factor (4x before V3 activation, then the configured V3 factor)
 * for blocks at the difficulty adjustment interval, and otherwise requires the
 * values to be the same.
 *
 * Always returns true on networks where min difficulty blocks are allowed,
 * such as regtest/testnet.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

/** V5 dual PoW — full check (including expensive KawPow progpow::verify). */
bool CheckBlockProofOfWork(const CBlockHeader& block, int height, const Consensus::Params& params);
/**
 * LoadBlockIndex path only: full cheap SHA PoW, structural KawPow checks.
 * Skips progpow::verify so cold starts stay fast as the dual-PoW chain grows.
 * New blocks from the network still use CheckBlockProofOfWork().
 */
bool CheckBlockProofOfWorkForIndexLoad(const CBlockHeader& block, int height, const Consensus::Params& params);
unsigned int GetNextGpuWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params);
/** V5.1 / V6 per-algo LWMA retarget. */
unsigned int GetNextWorkRequiredLwma(const CBlockIndex* pindexLast, const CBlockHeader* pblock, bool sha, const Consensus::Params& params);
unsigned int GetNextWorkRequiredLwmaForAlgo(const CBlockIndex* pindexLast, const CBlockHeader* pblock, PowAlgo algo, const Consensus::Params& params);
unsigned int GetBitsShaAtIndex(const CBlockIndex* pindex, const Consensus::Params& params);
unsigned int GetBitsGpuAtIndex(const CBlockIndex* pindex, const Consensus::Params& params);
unsigned int GetBitsRxAtIndex(const CBlockIndex* pindex, const Consensus::Params& params);
/** V6.1: effective RandomX pow limit at a given height (eased randomxPowLimit at/after
 *  nRandomXEaseActivationHeight, otherwise the shared powLimit). */
uint256 RandomXPowLimit(int height, const Consensus::Params& params);
unsigned int GetBitsForAlgoAtIndex(const CBlockIndex* pindex, PowAlgo algo, const Consensus::Params& params);
void UpdateDualPowBitsOnIndex(CBlockIndex& index, const CBlockHeader& block, const Consensus::Params& params);

#endif // BITCOIN_POW_H
