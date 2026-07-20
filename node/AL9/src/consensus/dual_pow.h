// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#ifndef BITCOIN_CONSENSUS_DUAL_POW_H
#define BITCOIN_CONSENSUS_DUAL_POW_H

#include <cstdint>

namespace Consensus {
struct Params;
}

class CBlockHeader;
class CBlockIndex;

/** V5 dual-PoW block header flag (KawPow fields present). Bit 17 — avoids BIP9 top bits (0x20000000). */
static constexpr int32_t BLOCK_VERSION_GPU_KAWPOW = 0x00020000;
/** V6 RandomX CPU PoW header flag. Bit 18. */
static constexpr int32_t BLOCK_VERSION_CPU_RANDOMX = 0x00040000;
/**
 * V6 extended-header marker. Signals the header carries the 64-bit nNonce64 +
 * mixHash (KawPow / RandomX) instead of the legacy 4-byte nNonce.
 *
 * Placed at bit 0 (0x00000001), OUTSIDE the BIP320 version-rolling domain
 * (0x1fffe000, bits 13-28). SHA ASIC AsicBoost rolling flips bits 13-28 (which
 * unavoidably includes the algo-signal bits 17/18), but can never set or clear
 * bit 0 — so the on-wire/on-disk header byte length stays deterministic even
 * when a SHA miner rolls bits 17/18. Only meaningful at/after the multi-algo
 * race activation height; below it the legacy bit-17 rule is used unchanged, so
 * V5 KawPow history and index-load/reindex are byte-identical.
 */
static constexpr int32_t BLOCK_VERSION_V6_EXTENDED = 0x00000001;

enum class PowAlgo : int {
    SHA256 = 0,
    KAWPOW = 1,
    RANDOMX = 2,
};

bool DualPowActive(int height, const Consensus::Params& params);
/** V6: simultaneous SHA + KawPow + RandomX race (no height windows). */
bool MultiAlgoRaceActive(int height, const Consensus::Params& params);

bool IsShaBlockHeight(int height, const Consensus::Params& params);
bool IsGpuBlockHeight(int height, const Consensus::Params& params);

/** First block of each 3-block window — hold difficulty (no retarget). Pre-race only. */
bool IsDualPowWindowStart(int height, const Consensus::Params& params);

/** Algo from header version/fields (race mode) or height schedule (pre-race). */
PowAlgo AlgoFromHeader(const CBlockHeader& header, int height, const Consensus::Params& params);
/** Algo of an indexed block (uses stored nVersion / KawPow fields + height schedule). */
PowAlgo AlgoAtIndex(const CBlockIndex* pindex, const Consensus::Params& params);

const char* PowAlgoName(PowAlgo algo);

struct ReplayForkAtHeight {
    bool require_sighash{false};
    uint32_t fork_id{0};
};

/** V4 / V5 / V5.1 / V6 replay fork ids by height. */
ReplayForkAtHeight ReplayForkForHeight(int height, const Consensus::Params& params);

#endif // BITCOIN_CONSENSUS_DUAL_POW_H
