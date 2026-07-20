// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/dual_pow.h>
#include <consensus/params.h>
#include <kawpow_check.h>
#include <primitives/block.h>
#include <randomx_check.h>
#include <uint256.h>
#include <util/check.h>

#include <vector>

int64_t PowTargetSpacingAtHeight(const Consensus::Params& params, int64_t height)
{
    if (params.nHashrateSubsidyActivationHeight > 0 &&
        height >= params.nHashrateSubsidyActivationHeight &&
        params.nPowPostForkTargetSpacing > 0) {
        return params.nPowPostForkTargetSpacing;
    }
    return params.nPowTargetSpacing;
}

static int64_t PowTargetTimespanAtHeight(const Consensus::Params& params, int64_t height)
{
    if (params.nPowRetargetV2ActivationHeight > 0 &&
        height >= params.nPowRetargetV2ActivationHeight) {
        return PowTargetSpacingAtHeight(params, height);
    }
    if (params.nPowRetargetActivationHeight > 0 &&
        height >= params.nPowRetargetActivationHeight &&
        params.nPowRetargetTimespan > 0) {
        return params.nPowRetargetTimespan;
    }
    return params.nPowTargetTimespan;
}

static int64_t DifficultyAdjustmentIntervalAtHeight(const Consensus::Params& params, int64_t height)
{
    const int64_t spacing = PowTargetSpacingAtHeight(params, height);
    if (spacing <= 0) return params.nPowTargetTimespan / params.nPowTargetSpacing;
    return PowTargetTimespanAtHeight(params, height) / spacing;
}

static void PowAdjustmentTimespanBounds(const Consensus::Params& params, int64_t height, int64_t nTargetTimespan, int64_t& smallest_timespan, int64_t& largest_timespan)
{
    int64_t factor_num = 4;
    int64_t factor_den = 1;
    if (params.nPowRetargetV3ActivationHeight > 0 &&
        height >= params.nPowRetargetV3ActivationHeight &&
        params.nPowRetargetV2ActivationHeight > 0 &&
        height >= params.nPowRetargetV2ActivationHeight &&
        nTargetTimespan == PowTargetSpacingAtHeight(params, height) &&
        params.nPowRetargetV3MaxFactorNum > 0 &&
        params.nPowRetargetV3MaxFactorDen > 0) {
        factor_num = params.nPowRetargetV3MaxFactorNum;
        factor_den = params.nPowRetargetV3MaxFactorDen;
    }
    smallest_timespan = nTargetTimespan * factor_den / factor_num;
    largest_timespan = nTargetTimespan * factor_num / factor_den;
}

static unsigned int CalculateNextWorkRequiredForTimespan(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params, int64_t nTargetTimespan, int64_t height)
{
    if (params.fPowNoRetargeting)
        return DualPowActive(height, params) ? GetBitsShaAtIndex(pindexLast, params) : pindexLast->nBits;

    // Limit adjustment step
    int64_t smallest_timespan = 0;
    int64_t largest_timespan = 0;
    PowAdjustmentTimespanBounds(params, height, nTargetTimespan, smallest_timespan, largest_timespan);
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < smallest_timespan)
        nActualTimespan = smallest_timespan;
    if (nActualTimespan > largest_timespan)
        nActualTimespan = largest_timespan;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(DualPowActive(height, params) ? GetBitsShaAtIndex(pindexLast, params) : pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    const int nHeightNext = pindexLast->nHeight + 1;
    // V6 multi-algo race: difficulty from header-claimed algo (independent LWMA tracks).
    if (MultiAlgoRaceActive(nHeightNext, params)) {
        const PowAlgo algo = pblock ? AlgoFromHeader(*pblock, nHeightNext, params) : PowAlgo::SHA256;
        return GetNextWorkRequiredLwmaForAlgo(pindexLast, pblock, algo, params);
    }
    // V5.1 per-algo LWMA retarget for both SHA and GPU at/after activation.
    if (params.nDualPowLwmaActivationHeight > 0 &&
        nHeightNext >= params.nDualPowLwmaActivationHeight &&
        DualPowActive(nHeightNext, params)) {
        return GetNextWorkRequiredLwma(pindexLast, pblock, IsShaBlockHeight(nHeightNext, params), params);
    }
    if (DualPowActive(nHeightNext, params) && IsGpuBlockHeight(nHeightNext, params)) {
        return GetNextGpuWorkRequired(pindexLast, pblock, params);
    }

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    const int64_t nTargetTimespan = PowTargetTimespanAtHeight(params, nHeightNext);
    const int64_t nDifficultyAdjustmentInterval = DifficultyAdjustmentIntervalAtHeight(params, nHeightNext);

    // Only change once per difficulty adjustment interval
    if (nHeightNext % nDifficultyAdjustmentInterval != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + PowTargetSpacingAtHeight(params, nHeightNext) * 2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % nDifficultyAdjustmentInterval != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return DualPowActive(nHeightNext, params) ? GetBitsShaAtIndex(pindex, params) : pindex->nBits;
            }
        }
        return DualPowActive(nHeightNext, params) ? GetBitsShaAtIndex(pindexLast, params) : pindexLast->nBits;
    }

    // Go back by the active difficulty window. A one-block interval needs the
    // previous block as the timestamp anchor so the elapsed spacing is real.
    int nHeightFirst = pindexLast->nHeight - (nDifficultyAdjustmentInterval - 1);
    if (nDifficultyAdjustmentInterval == 1 && pindexLast->pprev) {
        nHeightFirst = pindexLast->pprev->nHeight;
    }
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequiredForTimespan(pindexLast, pindexFirst->GetBlockTime(), params, nTargetTimespan, nHeightNext);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    return CalculateNextWorkRequiredForTimespan(pindexLast, nFirstBlockTime, params, params.nPowTargetTimespan, pindexLast->nHeight + 1);
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    // V5.1: the per-algo LWMA has no fixed per-step adjustment bound, so the
    // header-sync sanity check cannot constrain it. The exact nBits is still
    // fully verified against GetNextWorkRequired() in ContextualCheckBlockHeader.
    if (params.nDualPowLwmaActivationHeight > 0 &&
        height >= params.nDualPowLwmaActivationHeight) {
        return true;
    }

    const int64_t nTargetTimespan = PowTargetTimespanAtHeight(params, height);
    const int64_t nDifficultyAdjustmentInterval = DifficultyAdjustmentIntervalAtHeight(params, height);

    if (height % nDifficultyAdjustmentInterval == 0) {
        int64_t smallest_timespan = 0;
        int64_t largest_timespan = 0;
        PowAdjustmentTimespanBounds(params, height, nTargetTimespan, smallest_timespan, largest_timespan);

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= nTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= nTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

static void GpuPowAdjustmentTimespanBounds(const Consensus::Params& params, int64_t nTargetTimespan, int64_t& smallest_timespan, int64_t& largest_timespan)
{
    int64_t factor_num = params.nPowRetargetGpuMaxFactorNum > 0 ? params.nPowRetargetGpuMaxFactorNum : 2;
    int64_t factor_den = params.nPowRetargetGpuMaxFactorDen > 0 ? params.nPowRetargetGpuMaxFactorDen : 1;
    smallest_timespan = nTargetTimespan * factor_den / factor_num;
    largest_timespan = nTargetTimespan * factor_num / factor_den;
}

static const CBlockIndex* FindPrevBlockOfAlgo(const CBlockIndex* pindex, PowAlgo algo, const Consensus::Params& params)
{
    while (pindex != nullptr) {
        if (AlgoAtIndex(pindex, params) == algo) return pindex;
        pindex = pindex->pprev;
    }
    return nullptr;
}

unsigned int GetBitsShaAtIndex(const CBlockIndex* pindex, const Consensus::Params& params)
{
    if (!pindex) return UintToArith256(params.powLimit).GetCompact();
    if (pindex->nBitsSha != 0) return pindex->nBitsSha;
    if (AlgoAtIndex(pindex, params) == PowAlgo::SHA256) return pindex->nBits;
    const CBlockIndex* prev = FindPrevBlockOfAlgo(pindex->pprev, PowAlgo::SHA256, params);
    return prev ? GetBitsShaAtIndex(prev, params) : UintToArith256(params.powLimit).GetCompact();
}

unsigned int GetBitsGpuAtIndex(const CBlockIndex* pindex, const Consensus::Params& params)
{
    if (!DualPowActive(pindex ? pindex->nHeight : 0, params) &&
        !MultiAlgoRaceActive(pindex ? pindex->nHeight : 0, params)) {
        return params.nDualPowGpuSeedBits;
    }
    if (pindex && pindex->nBitsGpu != 0) return pindex->nBitsGpu;
    if (pindex && AlgoAtIndex(pindex, params) == PowAlgo::KAWPOW) return pindex->nBits;
    const CBlockIndex* prev = pindex ? FindPrevBlockOfAlgo(pindex->pprev, PowAlgo::KAWPOW, params) : nullptr;
    if (prev) return GetBitsGpuAtIndex(prev, params);
    return params.nDualPowGpuSeedBits;
}

// V6.1 RandomX difficulty ease helpers (height-gated). SHA/KawPow are never affected.
static bool RandomXEaseActive(int height, const Consensus::Params& params)
{
    return params.nRandomXEaseActivationHeight > 0 && height >= params.nRandomXEaseActivationHeight;
}

uint256 RandomXPowLimit(int height, const Consensus::Params& params)
{
    if (RandomXEaseActive(height, params) && !params.randomxPowLimit.IsNull()) {
        return params.randomxPowLimit;
    }
    return params.powLimit;
}

static unsigned int RandomXSeedBitsForHeight(int height, const Consensus::Params& params)
{
    if (RandomXEaseActive(height, params) && params.nMultiAlgoRandomXEaseSeedBits != 0) {
        return params.nMultiAlgoRandomXEaseSeedBits;
    }
    return params.nMultiAlgoRandomXSeedBits;
}

unsigned int GetBitsRxAtIndex(const CBlockIndex* pindex, const Consensus::Params& params)
{
    if (pindex && pindex->nBitsRx != 0) return pindex->nBitsRx;
    if (pindex && AlgoAtIndex(pindex, params) == PowAlgo::RANDOMX) return pindex->nBits;
    const CBlockIndex* prev = pindex ? FindPrevBlockOfAlgo(pindex->pprev, PowAlgo::RANDOMX, params) : nullptr;
    if (prev) return GetBitsRxAtIndex(prev, params);
    const int nextHeight = pindex ? pindex->nHeight + 1 : 0;
    const unsigned int seed = RandomXSeedBitsForHeight(nextHeight, params);
    return seed != 0 ? seed
                     : UintToArith256(RandomXPowLimit(nextHeight, params)).GetCompact();
}

unsigned int GetBitsForAlgoAtIndex(const CBlockIndex* pindex, PowAlgo algo, const Consensus::Params& params)
{
    switch (algo) {
    case PowAlgo::KAWPOW: return GetBitsGpuAtIndex(pindex, params);
    case PowAlgo::RANDOMX: return GetBitsRxAtIndex(pindex, params);
    case PowAlgo::SHA256:
    default: return GetBitsShaAtIndex(pindex, params);
    }
}

static unsigned int CalculateNextGpuWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params, int nHeightNext)
{
    if (params.fPowNoRetargeting) {
        return GetBitsGpuAtIndex(pindexLast, params);
    }
    if (IsDualPowWindowStart(nHeightNext, params)) {
        return GetBitsGpuAtIndex(pindexLast, params);
    }

    const int64_t nTargetTimespan = PowTargetSpacingAtHeight(params, nHeightNext);
    const CBlockIndex* pindexPrevGpu = FindPrevBlockOfAlgo(pindexLast, PowAlgo::KAWPOW, params);
    if (!pindexPrevGpu || !pindexPrevGpu->pprev) {
        return params.nDualPowGpuSeedBits;
    }
    const CBlockIndex* pindexFirstGpu = FindPrevBlockOfAlgo(pindexPrevGpu->pprev, PowAlgo::KAWPOW, params);
    if (!pindexFirstGpu) {
        return GetBitsGpuAtIndex(pindexPrevGpu, params);
    }

    int64_t smallest_timespan = 0;
    int64_t largest_timespan = 0;
    GpuPowAdjustmentTimespanBounds(params, nTargetTimespan, smallest_timespan, largest_timespan);
    int64_t nActualTimespan = pindexPrevGpu->GetBlockTime() - pindexFirstGpu->GetBlockTime();
    if (nActualTimespan < smallest_timespan) nActualTimespan = smallest_timespan;
    if (nActualTimespan > largest_timespan) nActualTimespan = largest_timespan;

    arith_uint256 bnNew;
    bnNew.SetCompact(GetBitsGpuAtIndex(pindexPrevGpu, params));
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    if (bnNew > bnPowLimit) bnNew = bnPowLimit;
    return bnNew.GetCompact();
}

unsigned int GetNextGpuWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    const int nHeightNext = pindexLast->nHeight + 1;
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    if (params.fPowAllowMinDifficultyBlocks && pblock &&
        pblock->GetBlockTime() > pindexLast->GetBlockTime() + PowTargetSpacingAtHeight(params, nHeightNext) * 2) {
        return nProofOfWorkLimit;
    }

    return CalculateNextGpuWorkRequired(pindexLast, params, nHeightNext);
}

// V5.1 per-algo Linear Weighted Moving Average (Zawy LWMA-1) retarget.
// Operates independently for SHA and GPU using only same-algo blocks mined
// at/after the LWMA activation height, so the noisy pre-fork difficulty does
// not pollute the window. Each block's solvetime is measured against its
// immediate predecessor (when mining for that height could start), clamped to
// [1, 6T], and weighted linearly by recency. Targets 'T' seconds between
// consecutive blocks overall (nPowPostForkTargetSpacing).
unsigned int GetNextWorkRequiredLwmaForAlgo(const CBlockIndex* pindexLast, const CBlockHeader* pblock, PowAlgo algo, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    const int nHeightNext = pindexLast->nHeight + 1;
    // RandomX may use a separate, easier pow limit at/after the ease activation height;
    // SHA/KawPow always use the shared powLimit. Governs both the LWMA floor clamp and the
    // bootstrap seed for this algo.
    const arith_uint256 bnAlgoPowLimit = (algo == PowAlgo::RANDOMX)
        ? UintToArith256(RandomXPowLimit(nHeightNext, params))
        : bnPowLimit;
    // Race mode: each algo targets 630s so three live algos race to ~210s overall.
    const int64_t T = MultiAlgoRaceActive(nHeightNext, params) && params.nMultiAlgoPerAlgoTargetSpacing > 0
                          ? params.nMultiAlgoPerAlgoTargetSpacing
                          : PowTargetSpacingAtHeight(params, nHeightNext);
    const int act = MultiAlgoRaceActive(nHeightNext, params)
                        ? params.nMultiAlgoRaceActivationHeight
                        : params.nDualPowLwmaActivationHeight;
    const int N = params.nLwmaWindow > 0 ? params.nLwmaWindow : 60;

    if (params.fPowNoRetargeting) {
        return GetBitsForAlgoAtIndex(pindexLast, algo, params);
    }
    if (params.fPowAllowMinDifficultyBlocks && pblock &&
        pblock->GetBlockTime() > pindexLast->GetBlockTime() + T * 2) {
        return bnAlgoPowLimit.GetCompact();
    }

    std::vector<const CBlockIndex*> blocks;
    for (const CBlockIndex* p = pindexLast; p != nullptr && p->nHeight >= act; p = p->pprev) {
        if (AlgoAtIndex(p, params) == algo) {
            blocks.push_back(p);
            if (static_cast<int>(blocks.size()) >= N) break;
        }
    }

    const int n = static_cast<int>(blocks.size());
    if (n == 0) {
        if (algo == PowAlgo::SHA256) return GetBitsShaAtIndex(pindexLast, params);
        if (algo == PowAlgo::RANDOMX) {
            const unsigned int seed = RandomXSeedBitsForHeight(nHeightNext, params);
            return seed != 0 ? seed : bnAlgoPowLimit.GetCompact();
        }
        return params.nDualPowLwmaGpuSeedBits != 0 ? params.nDualPowLwmaGpuSeedBits
                                                   : GetBitsGpuAtIndex(pindexLast, params);
    }

    arith_uint256 sumTarget = 0;
    int64_t weightedSolvetime = 0;
    for (int k = 1; k <= n; ++k) {
        const CBlockIndex* b = blocks[n - k];
        int64_t st = b->pprev ? (b->GetBlockTime() - b->pprev->GetBlockTime()) : T;
        if (st < 1) st = 1;
        if (st > 6 * T) st = 6 * T;
        weightedSolvetime += static_cast<int64_t>(k) * st;
        arith_uint256 tgt;
        tgt.SetCompact(GetBitsForAlgoAtIndex(b, algo, params));
        sumTarget += tgt;
    }
    if (weightedSolvetime < 1) weightedSolvetime = 1;

    const int64_t denom = T * static_cast<int64_t>(n) * (n + 1) / 2;
    arith_uint256 bnNew = sumTarget;
    if (algo == PowAlgo::RANDOMX) {
        // RandomX eased targets are ~1e6x larger than diff-1, so the SHA/KawPow order
        // (avg target, then * weightedSolvetime) overflows arith_uint256. Divide by the
        // denominator before multiplying to keep the intermediate < 2^256. Algebraically
        // the same ratio sumTarget*weightedSolvetime/(n*denom); only integer rounding
        // differs, and this branch is RandomX-only with no legacy blocks. RandomX targets
        // stay far larger than denom for any realistic CPU difficulty, so no underflow.
        bnNew /= static_cast<int64_t>(n);
        bnNew /= denom;
        bnNew *= weightedSolvetime;
    } else {
        bnNew /= static_cast<int64_t>(n);
        bnNew *= weightedSolvetime;
        bnNew /= denom;
    }

    if (bnNew == 0) bnNew = arith_uint256(1);
    if (bnNew > bnAlgoPowLimit) bnNew = bnAlgoPowLimit;
    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredLwma(const CBlockIndex* pindexLast, const CBlockHeader* pblock, bool sha, const Consensus::Params& params)
{
    return GetNextWorkRequiredLwmaForAlgo(pindexLast, pblock, sha ? PowAlgo::SHA256 : PowAlgo::KAWPOW, params);
}

namespace {

bool CheckBlockProofOfWorkInner(const CBlockHeader& block, int height, const Consensus::Params& params, bool full_kawpow)
{
    if (!DualPowActive(height, params) && !MultiAlgoRaceActive(height, params)) {
        return CheckProofOfWork(block.GetHash(), block.nBits, params);
    }

    if (MultiAlgoRaceActive(height, params)) {
        const PowAlgo algo = AlgoFromHeader(block, height, params);
        if (algo == PowAlgo::RANDOMX) {
            if (!full_kawpow) return !block.mixHash.IsNull();
            return CheckRandomXProofOfWork(block, height, params);
        }
        if (algo == PowAlgo::KAWPOW) {
            if (!block.IsGpuKawPowHeader()) return false;
            if (!full_kawpow) return !block.mixHash.IsNull();
            return CheckKawPowProofOfWork(block, height, params);
        }
        // SHA in race mode. AsicBoost version-rolling (BIP320, mask 0x1fffe000) can
        // flip the algo-signal bits 17/18 on an otherwise plain SHA block. That is now
        // tolerated: a genuine SHA block never carries the extended PoW fields
        // (nNonce64/mixHash) — they are only deserialized when the rolling-safe
        // BLOCK_VERSION_V6_EXTENDED bit is set — so AlgoFromHeader classified this as
        // SHA on field presence and the rolled bits 17/18 are just AsicBoost noise.
        // GetHash() likewise hashes the legacy 80-byte header (no ext fields present),
        // matching exactly what the miner hashed. The coinbase telemetry marker's algo
        // field is dual-bound to this classification in ContextualCheckBlock.
        return CheckProofOfWork(block.GetHash(), block.nBits, params);
    }

    if (IsShaBlockHeight(height, params)) {
        // Pre-activation (V6 height-gate fix): bit 18 (RandomX flag) is IGNORED here.
        // Historical SHA miners legally version-rolled bits 17/18 (BIP320 ASICBoost);
        // such blocks still hash/validate as plain SHA256 (see CBlockHeader::GetHash).
        // Rejecting on bit 18 here crashed index-load at height 13190 (proofs/02).
        if (block.IsGpuKawPowHeader()) {
            const bool has_kawpow_fields = block.nNonce64 != 0 || !block.mixHash.IsNull();
            const bool bit17_fix_active = params.nDualPowBit17ShaFixHeight > 0 &&
                                          height >= params.nDualPowBit17ShaFixHeight;
            if (has_kawpow_fields || !bit17_fix_active) return false;
        }
        return CheckProofOfWork(block.GetHash(), block.nBits, params);
    }
    if (!block.IsGpuKawPowHeader()) return false;
    if (!full_kawpow) {
        return !block.mixHash.IsNull();
    }
    return CheckKawPowProofOfWork(block, height, params);
}

} // namespace

bool CheckBlockProofOfWork(const CBlockHeader& block, int height, const Consensus::Params& params)
{
    return CheckBlockProofOfWorkInner(block, height, params, /*full_kawpow=*/true);
}

bool CheckBlockProofOfWorkForIndexLoad(const CBlockHeader& block, int height, const Consensus::Params& params)
{
    return CheckBlockProofOfWorkInner(block, height, params, /*full_kawpow=*/false);
}

void UpdateDualPowBitsOnIndex(CBlockIndex& index, const CBlockHeader& block, const Consensus::Params& params)
{
    index.nNonce64 = block.nNonce64;
    index.mixHash = block.mixHash;
    if (!DualPowActive(index.nHeight, params) && !MultiAlgoRaceActive(index.nHeight, params)) {
        index.nBitsSha = block.nBits;
        return;
    }
    const PowAlgo algo = AlgoFromHeader(block, index.nHeight, params);
    index.nBitsSha = index.pprev ? GetBitsShaAtIndex(index.pprev, params) : index.nBits;
    index.nBitsGpu = index.pprev ? GetBitsGpuAtIndex(index.pprev, params) : params.nDualPowGpuSeedBits;
    index.nBitsRx = index.pprev ? GetBitsRxAtIndex(index.pprev, params) : params.nMultiAlgoRandomXSeedBits;
    if (algo == PowAlgo::KAWPOW) {
        index.nBitsGpu = block.nBits;
    } else if (algo == PowAlgo::RANDOMX) {
        index.nBitsRx = block.nBits;
    } else {
        index.nBitsSha = block.nBits;
    }
}
