// Copyright (c) 2026 The HobbyHash Core developers
// V5 dual-PoW unit tests (AL10 native verify binary)

#include <consensus/amount.h>
#include <consensus/dual_pow.h>
#include <consensus/hashrate_subsidy.h>
#include <consensus/params.h>
#include <uint256.h>

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        ++g_failures; \
    } \
} while (0)

static Consensus::Params V5TestParams()
{
    Consensus::Params p;
    p.nSubsidyHalvingInterval = 840000;
    p.nHashrateSubsidyActivationHeight = 7000;
    p.nPowPostForkTargetSpacing = 210;
    p.nPowTargetSpacing = 210;
    p.nSubsidyHashrateFallbackHobc = 45;
    p.nDualPowActivationHeight = 150;
    p.nDualPowCycleLength = 6;
    p.nDualPowShaCount = 3;
    p.nDualPowGpuSeedBits = 0x207fffff;
    p.nPowRetargetGpuMaxFactorNum = 2;
    p.nPowRetargetGpuMaxFactorDen = 1;
    p.nDualPowReplayForkId = 0x00008000;
    p.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    p.fPowNoRetargeting = true;
    return p;
}

static void TestHeightRouting()
{
    const auto p = V5TestParams();
    CHECK(!DualPowActive(149, p), "pre-activation not dual");
    CHECK(DualPowActive(150, p), "activation height dual");
    CHECK(IsShaBlockHeight(149, p), "pre-activation always SHA");
    CHECK(IsShaBlockHeight(150, p), "150 mod6=0 SHA");
    CHECK(IsShaBlockHeight(151, p), "151 SHA");
    CHECK(IsShaBlockHeight(152, p), "152 SHA");
    CHECK(IsGpuBlockHeight(153, p), "153 GPU");
    CHECK(IsGpuBlockHeight(154, p), "154 GPU");
    CHECK(IsGpuBlockHeight(155, p), "155 GPU");
    CHECK(IsShaBlockHeight(156, p), "156 SHA cycle repeat");
    CHECK(IsDualPowWindowStart(150, p), "window start 150");
    CHECK(IsDualPowWindowStart(153, p), "window start 153");
    CHECK(!IsDualPowWindowStart(151, p), "151 not window start");
}

static void TestSubsidyRouting()
{
    const auto p = V5TestParams();
    CHECK(Consensus::GetBlockSubsidy(152, p, nullptr) == 45 * COIN, "SHA height fallback 45");
    CHECK(Consensus::GetBlockSubsidy(153, p, nullptr) == 45 * COIN, "GPU height 45");
    CHECK(Consensus::GetBlockSubsidy(840002, p, nullptr) == 45 * COIN, "SHA height no GPU halving");
    const CAmount gpu_halved = Consensus::GetBlockSubsidy(840003, p, nullptr);
    CHECK(gpu_halved == 22'500'000'00, "GPU height halving applies (22.5 HOBC)");
}

static void TestReplayForkId()
{
    const auto p = V5TestParams();
    const auto pre = ReplayForkForHeight(149, p);
    CHECK(!pre.require_sighash, "pre dual no replay");
    const auto at = ReplayForkForHeight(150, p);
    CHECK(at.require_sighash, "dual activation requires sighash");
    CHECK(at.fork_id == 0x00008000U, "V5 fork id at activation");
}

static void TestGpuRetargetHold()
{
    const auto p = V5TestParams();
    CHECK(IsDualPowWindowStart(153, p), "GPU retarget hold at window start 153");
    CHECK(!IsDualPowWindowStart(154, p), "154 retarget allowed");
}

int main()
{
    TestHeightRouting();
    TestSubsidyRouting();
    TestReplayForkId();
    TestGpuRetargetHold();

    if (g_failures == 0) {
        std::printf("PASS: v5_dual_pow_tests (all checks ok)\n");
        return 0;
    }
    std::fprintf(stderr, "FAIL: v5_dual_pow_tests (%d failures)\n", g_failures);
    return 1;
}
