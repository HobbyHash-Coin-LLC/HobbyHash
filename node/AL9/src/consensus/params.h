// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <script/verify_flags.h>
#include <uint256.h>

#include <array>
#include <chrono>
#include <limits>
#include <map>
#include <vector>

namespace Consensus {

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    DEPLOYMENT_SEGWIT,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_SEGWIT; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_TAPROOT, // Deployment of Schnorr/Taproot (BIPs 340-342)
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};
    /** Period of blocks to check signalling in (usually retarget period, ie params.DifficultyAdjustmentInterval()) */
    uint32_t period{2016};
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t threshold{1916};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, script_verify_flags> script_flag_exceptions;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV and segwit activations. */
    int MinBIP9WarningHeight;
    std::array<BIP9Deployment,MAX_VERSION_BITS_DEPLOYMENTS> vDeployments;
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    /**
      * Enforce BIP94 timewarp attack mitigation. On testnet4 this also enforces
      * the block storm mitigation.
      */
    bool enforce_BIP94;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int nPowRetargetActivationHeight{0};
    int64_t nPowRetargetTimespan{0};
    int nPowRetargetV2ActivationHeight{0};
    int64_t nPowRetargetV2Timespan{0};
    int nPowRetargetV3ActivationHeight{0};
    int64_t nPowRetargetV3MaxFactorNum{0};
    int64_t nPowRetargetV3MaxFactorDen{0};

    /** V4 hard fork: hashrate-linked subsidy activates at this height (mainnet 7000). */
    int nHashrateSubsidyActivationHeight{0};
    /** Block spacing in seconds after fork (210 = 3.5 min). */
    int64_t nPowPostForkTargetSpacing{210};
    /** Rolling window for subsidy hash average (default 5 hours). */
    int64_t nSubsidyHashrateWindowSeconds{18000};
    int nSubsidyHashrateWindowMinBlocks{2};
    int nSubsidyHashrateFallbackHobc{45};
    /** Replay protection fork id (SIGHASH_FORKID). */
    uint32_t nReplayForkId{0};

    /** V5 dual PoW: KawPow + SHA alternating blocks. */
    int nDualPowActivationHeight{0};
    int nDualPowCycleLength{6};
    int nDualPowShaCount{3};
    unsigned int nDualPowGpuSeedBits{0x1d00f455};
    /** At/after this height, ignore a stray GPU/KawPow version flag (bit 17,
     *  0x00020000) on SHA block heights. SHA miners (BIP320 ASICBoost) legally
     *  version-roll bit 17, which previously caused valid SHA solves to be
     *  rejected as high-hash. 0 = disabled (pre-fix strict behavior). */
    int nDualPowBit17ShaFixHeight{0};
    int64_t nPowRetargetGpuMaxFactorNum{2};
    int64_t nPowRetargetGpuMaxFactorDen{1};
    /** V5 replay fork id and P2P magic applied at dual pow activation. */
    uint32_t nDualPowReplayForkId{0};

    /** Per-algo LWMA difficulty retarget (HobbyHash V5.1 fork).
     *  At/after nDualPowLwmaActivationHeight, both SHA and GPU difficulty are
     *  retargeted with a Linear Weighted Moving Average over the last
     *  nLwmaWindow same-algo blocks (post-activation only), targeting
     *  nPowPostForkTargetSpacing seconds between consecutive blocks overall.
     *  nDualPowLwmaGpuSeedBits seeds only the very first post-fork GPU block;
     *  the LWMA self-corrects thereafter. nDualPowLwmaReplayForkId provides
     *  transaction replay protection across the fork (0 = disabled). */
    int nDualPowLwmaActivationHeight{0};
    int nLwmaWindow{60};
    unsigned int nDualPowLwmaGpuSeedBits{0};
    uint32_t nDualPowLwmaReplayForkId{0};

    /** V6 multi-algo race: SHA + KawPow + RandomX simultaneous (mainnet 16700). */
    int nMultiAlgoRaceActivationHeight{0};
    /** Per-algo LWMA target spacing in race mode (630 → ~210s overall with 3 live). */
    int64_t nMultiAlgoPerAlgoTargetSpacing{630};
    unsigned int nMultiAlgoRandomXSeedBits{0};
    uint32_t nMultiAlgoRaceReplayForkId{0};

    /** V6.1 RandomX difficulty ease (gated at nRandomXEaseActivationHeight).
     *  RandomX at diff-1 (shared powLimit, ~2^32 work) is unsolvable for CPUs, so at/after
     *  this height RandomX uses a separate, much easier pow limit (randomxPowLimit) and
     *  bootstrap seed (nMultiAlgoRandomXEaseSeedBits). SHA and KawPow are unaffected and
     *  keep the shared powLimit. Below the activation height RandomX behaves exactly as
     *  before (powLimit / nMultiAlgoRandomXSeedBits), so this is inert on deployed nodes
     *  until the height is reached. 0 / null disables the ease (legacy behavior). */
    int nRandomXEaseActivationHeight{0};
    uint256 randomxPowLimit;
    unsigned int nMultiAlgoRandomXEaseSeedBits{0};

    std::chrono::seconds PowTargetSpacing() const
    {
        return std::chrono::seconds{nPowTargetSpacing};
    }
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * If true, witness commitments contain a payload equal to a Bitcoin Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
