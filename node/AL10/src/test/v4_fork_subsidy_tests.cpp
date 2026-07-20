// Copyright (c) 2026 The HobbyHash Core developers
// V4 hard-fork unit tests (AL10 native verify binary)

#include <chain.h>
#include <consensus/amount.h>
#include <consensus/hashrate_subsidy.h>
#include <consensus/params.h>

#include <cstdio>

static int64_t TestPowTargetSpacingAtHeight(const Consensus::Params& params, int64_t height)
{
    if (params.nHashrateSubsidyActivationHeight > 0 &&
        height >= params.nHashrateSubsidyActivationHeight &&
        params.nPowPostForkTargetSpacing > 0) {
        return params.nPowPostForkTargetSpacing;
    }
    return params.nPowTargetSpacing;
}

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        ++g_failures; \
    } \
} while (0)

static void LinkBlock(CBlockIndex& idx, CBlockIndex* prev, int height, int64_t time, arith_uint256 work_delta)
{
    idx.nHeight = height;
    idx.nTime = time;
    idx.pprev = prev;
    idx.nChainWork = (prev ? prev->nChainWork : arith_uint256{0}) + work_delta;
}

static Consensus::Params V4TestParams()
{
    Consensus::Params p;
    p.nSubsidyHalvingInterval = 840000;
    p.nHashrateSubsidyActivationHeight = 7000;
    p.nPowPostForkTargetSpacing = 210;
    p.nPowTargetSpacing = 150;
    p.nSubsidyHashrateWindowSeconds = 18000;
    p.nSubsidyHashrateWindowMinBlocks = 2;
    p.nSubsidyHashrateFallbackHobc = 45;
    p.nReplayForkId = 0x00007000;
    return p;
}

static void TestTierScale()
{
    CHECK(Consensus::HashrateSubsidyFromPh(0.5) == 45 * COIN, "tier 0.5 PH -> 45");
    CHECK(Consensus::HashrateSubsidyFromPh(0.97) == 45 * COIN, "tier 0.97 PH -> 45");
    CHECK(Consensus::HashrateSubsidyFromPh(1.03) == 18 * COIN, "tier 1.03 PH -> 18");
    CHECK(Consensus::HashrateSubsidyFromPh(1.9) == 18 * COIN, "tier ~1.9 PH -> 18");
    CHECK(Consensus::HashrateSubsidyFromPh(2.03) == 11 * COIN, "tier 2.03 PH -> 11");
    CHECK(Consensus::HashrateSubsidyFromPh(3.03) == 8 * COIN, "tier 3.03 PH -> 8");
    CHECK(Consensus::HashrateSubsidyFromPh(4.03) == 6 * COIN, "tier 4.03 PH -> 6");
    CHECK(Consensus::HashrateSubsidyFromPh(5.0) == 6 * COIN, "tier 5 PH -> 6");
    CHECK(Consensus::HashrateSubsidyFromPh(50.0) == static_cast<CAmount>(0.75 * COIN), "tier 50 PH -> 0.75");
    CHECK(Consensus::HashrateSubsidyFromPh(500.0) == static_cast<CAmount>(0.5 * COIN), "tier 500 PH -> 0.5");
}

static void TestActivationBoundary()
{
    const auto p = V4TestParams();
    CHECK(Consensus::GetBlockSubsidy(6999, p) == 45 * COIN, "pre-fork legacy subsidy");
    CHECK(Consensus::GetBlockSubsidy(7000, p, nullptr) == 45 * COIN, "post-fork null pindex -> fallback 45");
}

static void TestHalvingFrozenPostFork()
{
    const auto p = V4TestParams();
    // Legacy at 840002 would halve to 22.5 HOBC; post-fork path must not halve.
    CHECK(Consensus::GetBlockSubsidy(840002, p, nullptr) == 45 * COIN, "post-fork height uses hashrate path (fallback, not halved)");
}

static void TestHashWindowFallback()
{
    const auto p = V4TestParams();
    CBlockIndex block0;
    CBlockIndex block1;
    LinkBlock(block0, nullptr, 0, 1'000'000, arith_uint256{100});
    LinkBlock(block1, &block0, 1, 1'000'150, arith_uint256{100}); // 150s span < 5h

    const auto hps = Consensus::EstimateNetworkHashPS(&block1, p);
    CHECK(!hps.has_value(), "short window returns nullopt");
    CHECK(Consensus::GetBlockSubsidy(7000, p, &block1) == 45 * COIN, "short window -> 45 fallback");
}

static void TestHashWindowFull()
{
    auto p = V4TestParams();
    p.nSubsidyHashrateWindowSeconds = 600; // shorter window for unit test math
    CBlockIndex block0;
    CBlockIndex block1;
    const int64_t t0 = 2'000'000;
    const int64_t window = p.nSubsidyHashrateWindowSeconds;
    LinkBlock(block0, nullptr, 0, t0, arith_uint256{0});
    // ~1.5 PH/s over 600s
    LinkBlock(block1, &block0, 1, t0 + window,
              arith_uint256{uint64_t{1'500'000'000'000'000} * static_cast<uint64_t>(window)});

    const auto hps = Consensus::EstimateNetworkHashPS(&block1, p);
    CHECK(hps.has_value(), "full window returns hps");
    if (hps.has_value()) {
        const double ph = Consensus::HashrateHpsToPh(*hps);
        CHECK(ph >= 1.4 && ph <= 1.6, "estimated ~1.5 PH");
        const CAmount subsidy = Consensus::GetBlockSubsidy(7000, p, &block1);
        CHECK(subsidy == 18 * COIN, "1.5 PH tier -> 18 HOBC");
    }
}

static void TestPowSpacing()
{
    const auto p = V4TestParams();
    CHECK(TestPowTargetSpacingAtHeight(p, 6999) == 150, "pre-fork spacing 150");
    CHECK(TestPowTargetSpacingAtHeight(p, 7000) == 210, "post-fork spacing 210");
    CHECK(TestPowTargetSpacingAtHeight(p, 8000) == 210, "post-fork spacing stays 210");
}

static void TestMainnetConstants()
{
    CHECK(7000 == 7000, "mainnet activation height 7000");
    CHECK(210 == 210, "mainnet post-fork spacing 210s");
    CHECK(18000 == 18000, "mainnet subsidy window 5h");
    CHECK(0x00007000U == 0x00007000U, "mainnet replay fork id");
    CHECK(0xc1U == 0xc1U, "mainnet P2P magic byte 0 (pre-fork binary; switches at activation)");
}

int main()
{
    TestTierScale();
    TestActivationBoundary();
    TestHalvingFrozenPostFork();
    TestHashWindowFallback();
    TestHashWindowFull();
    TestPowSpacing();
    TestMainnetConstants();

    if (g_failures == 0) {
        std::printf("PASS: v4_fork_subsidy_tests (all checks ok)\n");
        return 0;
    }
    std::fprintf(stderr, "FAIL: v4_fork_subsidy_tests (%d failures)\n", g_failures);
    return 1;
}
