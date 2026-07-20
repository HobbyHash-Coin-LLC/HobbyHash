// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#include <consensus/hashrate_subsidy.h>

#include <chain.h>
#include <consensus/amount.h>
#include <consensus/dual_pow.h>
#include <consensus/params.h>
#include <arith_uint256.h>

#include <algorithm>
#include <cmath>

namespace Consensus {
namespace {

struct SubsidyKnot {
    double ph;
    double hobc;
};

constexpr SubsidyKnot KNOTS[] = {
    {0.50, 45.0},
    {0.97, 45.0},
    {1.03, 18.0},
    {1.97, 18.0},
    {2.03, 11.0},
    {2.97, 11.0},
    {3.03, 8.0},
    {3.97, 8.0},
    {4.03, 6.0},
    {4.97, 6.0},
    {5.03, 6.0},
    {50.0, 0.75},
    {500.0, 0.50},
};

double Smoothstep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double LogLerpPh(double ph, double ph0, double ph1, double r0, double r1)
{
    if (ph <= ph0) return r0;
    if (ph >= ph1) return r1;
    const double t = (std::log10(ph) - std::log10(ph0)) / (std::log10(ph1) - std::log10(ph0));
    return r0 + (r1 - r0) * Smoothstep(t);
}

double SubsidyHobcFromPh(double ph)
{
    if (ph <= KNOTS[0].ph) return KNOTS[0].hobc;
    for (size_t i = 0; i + 1 < std::size(KNOTS); ++i) {
        if (ph <= KNOTS[i + 1].ph) {
            return LogLerpPh(ph, KNOTS[i].ph, KNOTS[i + 1].ph, KNOTS[i].hobc, KNOTS[i + 1].hobc);
        }
    }
    return KNOTS[std::size(KNOTS) - 1].hobc;
}

CAmount LegacyBlockSubsidy(int nHeight, const Params& consensusParams)
{
    if (nHeight == 1) {
        return 8'400'000 * COIN;
    }
    if (nHeight < 2) {
        return 0;
    }

    const int halvings = (nHeight - 2) / consensusParams.nSubsidyHalvingInterval;
    if (halvings >= 64) {
        return 0;
    }

    CAmount nSubsidy = 45 * COIN;
    nSubsidy >>= halvings;
    return nSubsidy;
}

CAmount FallbackSubsidy(const Params& params)
{
    const int hobc = params.nSubsidyHashrateFallbackHobc > 0 ? params.nSubsidyHashrateFallbackHobc : 45;
    return static_cast<CAmount>(hobc) * COIN;
}

} // namespace

double HashrateHpsToPh(double hps)
{
    if (hps <= 0.0) return 0.0;
    return hps / 1'000'000'000'000'000.0;
}

CAmount HashrateSubsidyFromPh(double network_ph)
{
    const double hobc = SubsidyHobcFromPh(std::max(network_ph, KNOTS[0].ph));
    return static_cast<CAmount>(std::llround(hobc * COIN));
}

std::optional<double> EstimateNetworkHashPS(const CBlockIndex* pindexPrev, const Params& params)
{
    if (pindexPrev == nullptr || pindexPrev->nHeight < 1) {
        return std::nullopt;
    }

    const int64_t window_seconds = params.nSubsidyHashrateWindowSeconds > 0
        ? params.nSubsidyHashrateWindowSeconds
        : 18000;
    const int min_blocks = params.nSubsidyHashrateWindowMinBlocks > 0
        ? params.nSubsidyHashrateWindowMinBlocks
        : 2;

    const CBlockIndex* pindexTip = pindexPrev;
    const CBlockIndex* pindexWalk = pindexPrev;
    int blocks = 1;

    while (pindexWalk->pprev != nullptr) {
        const int64_t span = pindexTip->GetBlockTime() - pindexWalk->pprev->GetBlockTime();
        if (span >= window_seconds && blocks >= min_blocks) {
            break;
        }
        pindexWalk = pindexWalk->pprev;
        ++blocks;
        if (pindexWalk->nHeight < 0) {
            break;
        }
    }

    if (blocks < min_blocks) {
        return std::nullopt;
    }

    const int64_t time_diff = pindexTip->GetBlockTime() - pindexWalk->GetBlockTime();
    if (time_diff < window_seconds) {
        return std::nullopt;
    }

    const int64_t clamped_time = std::max<int64_t>(time_diff, 1);
    arith_uint256 work_diff = pindexTip->nChainWork - pindexWalk->nChainWork;
    return work_diff.getdouble() / static_cast<double>(clamped_time);
}

CAmount GpuBlockSubsidy(int nHeight, const Params& consensusParams)
{
    if (nHeight < 2) return 0;
    const int halvings = (nHeight - 2) / consensusParams.nSubsidyHalvingInterval;
    if (halvings >= 64) return 0;
    CAmount nSubsidy = 45 * COIN;
    nSubsidy >>= halvings;
    return nSubsidy;
}

CAmount GetBlockSubsidy(int nHeight, const Params& consensusParams, const CBlockIndex* pindexPrev)
{
    if (consensusParams.nHashrateSubsidyActivationHeight <= 0 ||
        nHeight < consensusParams.nHashrateSubsidyActivationHeight) {
        return LegacyBlockSubsidy(nHeight, consensusParams);
    }

    // V6: flat 45 HOBC (halving) for every algo — no TH/PH tiers.
    if (MultiAlgoRaceActive(nHeight, consensusParams)) {
        return GpuBlockSubsidy(nHeight, consensusParams);
    }

    if (DualPowActive(nHeight, consensusParams) && IsGpuBlockHeight(nHeight, consensusParams)) {
        return GpuBlockSubsidy(nHeight, consensusParams);
    }

    if (pindexPrev == nullptr) {
        return FallbackSubsidy(consensusParams);
    }

    const auto hps = EstimateNetworkHashPS(pindexPrev, consensusParams);
    if (!hps.has_value()) {
        return FallbackSubsidy(consensusParams);
    }

    return HashrateSubsidyFromPh(HashrateHpsToPh(*hps));
}

} // namespace Consensus
