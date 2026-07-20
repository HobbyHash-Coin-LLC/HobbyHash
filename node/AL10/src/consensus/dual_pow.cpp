// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#include <consensus/dual_pow.h>

#include <chain.h>
#include <consensus/params.h>
#include <primitives/block.h>

bool DualPowActive(int height, const Consensus::Params& params)
{
    return params.nDualPowActivationHeight > 0 && height >= params.nDualPowActivationHeight;
}

bool MultiAlgoRaceActive(int height, const Consensus::Params& params)
{
    return params.nMultiAlgoRaceActivationHeight > 0 &&
           height >= params.nMultiAlgoRaceActivationHeight;
}

bool IsShaBlockHeight(int height, const Consensus::Params& params)
{
    // Pre-V6 schedule only. After multi-algo race activation, algo is header-based
    // (AlgoFromHeader / AlgoAtIndex) — these helpers must not be used for exclusivity.
    if (MultiAlgoRaceActive(height, params)) return true;
    if (!DualPowActive(height, params)) return true;
    return (height % params.nDualPowCycleLength) < params.nDualPowShaCount;
}

bool IsGpuBlockHeight(int height, const Consensus::Params& params)
{
    if (MultiAlgoRaceActive(height, params)) return false;
    if (!DualPowActive(height, params)) return false;
    return !IsShaBlockHeight(height, params);
}

bool IsDualPowWindowStart(int height, const Consensus::Params& params)
{
    if (MultiAlgoRaceActive(height, params)) return false;
    if (!DualPowActive(height, params)) return false;
    const int mod = height % params.nDualPowCycleLength;
    return mod == 0 || mod == params.nDualPowShaCount;
}

static PowAlgo AlgoFromVersionFields(int32_t nVersion, uint64_t nNonce64, const uint256& mixHash)
{
    // V6: a genuine KawPow/RandomX header always carries the extended PoW fields
    // (nNonce64 / mixHash) — CheckRandomXProofOfWork rejects a null mixHash, and
    // KawPow carries its mix digest. A plain SHA header never populates them.
    // Classify on FIELD PRESENCE, not on the raw algo-signal bits 17/18: a SHA
    // ASIC legally version-rolls bits 13-28 (BIP320 AsicBoost), which can set
    // bits 17/18 on an otherwise plain SHA block. The extended fields are only
    // ever deserialized when the rolling-safe BLOCK_VERSION_V6_EXTENDED bit is
    // set (see block.h / ReadBlockHeaderNonceFields), so a rolled SHA header
    // decodes with no fields and is classified SHA here regardless of bits 17/18.
    const bool has_ext_fields = (nNonce64 != 0 || !mixHash.IsNull());
    if (has_ext_fields) {
        if (nVersion & BLOCK_VERSION_CPU_RANDOMX) {
            return PowAlgo::RANDOMX;
        }
        if (nVersion & BLOCK_VERSION_GPU_KAWPOW) {
            return PowAlgo::KAWPOW;
        }
    }
    return PowAlgo::SHA256;
}

PowAlgo AlgoFromHeader(const CBlockHeader& header, int height, const Consensus::Params& params)
{
    if (MultiAlgoRaceActive(height, params)) {
        return AlgoFromVersionFields(header.nVersion, header.nNonce64, header.mixHash);
    }
    if (DualPowActive(height, params) && IsGpuBlockHeight(height, params)) {
        return PowAlgo::KAWPOW;
    }
    return PowAlgo::SHA256;
}

PowAlgo AlgoAtIndex(const CBlockIndex* pindex, const Consensus::Params& params)
{
    if (!pindex) return PowAlgo::SHA256;
    if (MultiAlgoRaceActive(pindex->nHeight, params)) {
        return AlgoFromVersionFields(pindex->nVersion, pindex->nNonce64, pindex->mixHash);
    }
    if (DualPowActive(pindex->nHeight, params) && IsGpuBlockHeight(pindex->nHeight, params)) {
        return PowAlgo::KAWPOW;
    }
    return PowAlgo::SHA256;
}

const char* PowAlgoName(PowAlgo algo)
{
    switch (algo) {
    case PowAlgo::KAWPOW: return "kawpow";
    case PowAlgo::RANDOMX: return "randomx";
    case PowAlgo::SHA256:
    default: return "sha256";
    }
}

ReplayForkAtHeight ReplayForkForHeight(int height, const Consensus::Params& params)
{
    ReplayForkAtHeight out{};
    if (params.nMultiAlgoRaceActivationHeight > 0 &&
        height >= params.nMultiAlgoRaceActivationHeight &&
        params.nMultiAlgoRaceReplayForkId != 0) {
        out.require_sighash = true;
        out.fork_id = params.nMultiAlgoRaceReplayForkId;
        return out;
    }
    if (params.nDualPowLwmaActivationHeight > 0 &&
        height >= params.nDualPowLwmaActivationHeight &&
        params.nDualPowLwmaReplayForkId != 0) {
        out.require_sighash = true;
        out.fork_id = params.nDualPowLwmaReplayForkId;
        return out;
    }
    if (params.nDualPowActivationHeight > 0 &&
        height >= params.nDualPowActivationHeight &&
        params.nDualPowReplayForkId != 0) {
        out.require_sighash = true;
        out.fork_id = params.nDualPowReplayForkId;
        return out;
    }
    if (params.nHashrateSubsidyActivationHeight > 0 &&
        height >= params.nHashrateSubsidyActivationHeight &&
        params.nReplayForkId != 0) {
        out.require_sighash = true;
        out.fork_id = params.nReplayForkId;
    }
    return out;
}
