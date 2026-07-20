// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/dual_pow.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;

    // Here (and below): expected_nbits is calculated in
    // CalculateNextWorkRequired(); redoing the calculation here would be just
    // reimplementing the same code that is written in pow.cpp. Rather than
    // copy that code, we just hardcode the expected result.
    unsigned int expected_nbits = 0x1d00d86aU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    unsigned int expected_nbits = 0x1d00ffffU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    unsigned int expected_nbits = 0x1c0168fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that reducing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits-1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    unsigned int expected_nbits = 0x1d00e1fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that increasing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits+1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p2 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p3 = &blocks[m_rng.randrange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

// ---- V5.1 per-algo LWMA retarget tests ----

static Consensus::Params LwmaTestParams()
{
    Consensus::Params p{};
    p.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    p.nPowTargetSpacing = 210;
    p.nPowTargetTimespan = 14 * 24 * 60 * 60;
    p.nPowPostForkTargetSpacing = 210;
    p.nHashrateSubsidyActivationHeight = 1; // so PowTargetSpacingAtHeight() == 210
    p.nDualPowActivationHeight = 60;
    p.nDualPowCycleLength = 6;
    p.nDualPowShaCount = 3;
    p.nDualPowGpuSeedBits = 0x1d00ffff;
    p.nDualPowLwmaActivationHeight = 60;
    p.nLwmaWindow = 30;
    p.nDualPowLwmaGpuSeedBits = 0x1d00ee00;
    p.fPowNoRetargeting = false;
    p.fPowAllowMinDifficultyBlocks = false;
    return p;
}

// Deterministic expected solvetime for a given target and hashrate (hashes/s),
// clamped to mirror the consensus [1, 6T] clamp.
static int64_t LwmaExpectedSolvetime(uint32_t bits, double H, int64_t T)
{
    arith_uint256 t;
    t.SetCompact(bits);
    const double work = std::ldexp(1.0, 256) / (t.getdouble() + 1.0);
    double st = work / H;
    if (st < 1.0) st = 1.0;
    if (st > 6.0 * T) st = 6.0 * T;
    return static_cast<int64_t>(st + 0.5);
}

BOOST_AUTO_TEST_CASE(lwma_seed_and_convergence)
{
    const Consensus::Params p = LwmaTestParams();
    const int64_t T = 210;
    const int total = 660;
    std::vector<CBlockIndex> b(total);

    // Pre-activation chain (all SHA), steady spacing.
    for (int h = 0; h < p.nDualPowActivationHeight; ++h) {
        b[h].pprev = h ? &b[h - 1] : nullptr;
        b[h].nHeight = h;
        b[h].nTime = 1700000000u + static_cast<unsigned int>(h * T);
        b[h].nBits = 0x1c4688cf;
        b[h].nBitsSha = 0x1c4688cf;
        b[h].nBitsGpu = p.nDualPowGpuSeedBits;
    }

    // Mine post-activation blocks deterministically. Different hashrate per algo
    // so each converges to its own difficulty while overall spacing -> 210s.
    const double H_sha = 5.2e9;
    const double H_gpu = 3.3e8;
    bool seed_checked = false;
    for (int h = p.nDualPowActivationHeight; h < total; ++h) {
        CBlockIndex* prev = &b[h - 1];
        const bool sha = IsShaBlockHeight(h, p);
        const unsigned int bits = GetNextWorkRequiredLwma(prev, nullptr, sha, p);

        // First post-activation GPU block must equal the configured seed.
        if (!sha && !seed_checked) {
            BOOST_CHECK_EQUAL(bits, p.nDualPowLwmaGpuSeedBits);
            seed_checked = true;
        }

        const int64_t st = LwmaExpectedSolvetime(bits, sha ? H_sha : H_gpu, T);
        b[h].pprev = prev;
        b[h].nHeight = h;
        b[h].nTime = prev->nTime + static_cast<unsigned int>(st);
        b[h].nBits = bits;
        if (IsGpuBlockHeight(h, p)) {
            b[h].nBitsGpu = bits;
            b[h].nBitsSha = GetBitsShaAtIndex(prev, p);
        } else {
            b[h].nBitsSha = bits;
            b[h].nBitsGpu = GetBitsGpuAtIndex(prev, p);
        }
    }

    BOOST_CHECK(seed_checked);

    // After convergence, overall spacing should be near the 210s target.
    int64_t sum = 0; int cnt = 0;
    for (int h = total - 100; h < total; ++h) { sum += b[h].nTime - b[h - 1].nTime; ++cnt; }
    const double mean_spacing = static_cast<double>(sum) / cnt;
    BOOST_CHECK_MESSAGE(mean_spacing > 185 && mean_spacing < 235,
                        "converged overall spacing " << mean_spacing << "s not within [185,235]");

    // Per-algo difficulty should be stable over the last several same-algo blocks.
    auto spread = [&](bool sha) {
        arith_uint256 mn, mx; bool first = true;
        int seen = 0;
        for (int h = total - 1; h >= p.nDualPowActivationHeight && seen < 10; --h) {
            if (IsShaBlockHeight(h, p) != sha) continue;
            arith_uint256 t; t.SetCompact(b[h].nBits);
            if (first) { mn = mx = t; first = false; }
            else { if (t < mn) mn = t; if (mx < t) mx = t; }
            ++seen;
        }
        // ratio mx/mn as double
        return mx.getdouble() / (mn.getdouble() + 1.0);
    };
    BOOST_CHECK_MESSAGE(spread(true) < 1.6, "SHA difficulty spread too large: " << spread(true));
    BOOST_CHECK_MESSAGE(spread(false) < 1.6, "GPU difficulty spread too large: " << spread(false));
}

BOOST_AUTO_TEST_CASE(lwma_direction)
{
    const Consensus::Params p = LwmaTestParams();
    const int64_t T = 210;
    const int total = 200;

    auto build = [&](int64_t sha_solvetime) {
        auto blocks = std::make_shared<std::vector<CBlockIndex>>(total);
        auto& b = *blocks;
        const unsigned int base_bits = 0x1c4688cf;
        for (int h = 0; h < total; ++h) {
            b[h].pprev = h ? &b[h - 1] : nullptr;
            b[h].nHeight = h;
            const bool sha = IsShaBlockHeight(h, p);
            // SHA blocks separated by sha_solvetime; GPU steady at T.
            const int64_t st = (h == 0) ? T : (sha ? sha_solvetime : T);
            b[h].nTime = h ? b[h - 1].nTime + static_cast<unsigned int>(st) : 1700000000u;
            b[h].nBits = base_bits;
            b[h].nBitsSha = base_bits;
            b[h].nBitsGpu = p.nDualPowGpuSeedBits;
        }
        return blocks;
    };

    arith_uint256 avg; avg.SetCompact(0x1c4688cf);

    // Too-fast SHA blocks -> next target should be harder (smaller) than avg.
    {
        auto blocks = build(T / 2);
        const unsigned int next = GetNextWorkRequiredLwma(&blocks->at(total - 1), nullptr, /*sha=*/true, p);
        arith_uint256 nt; nt.SetCompact(next);
        BOOST_CHECK_MESSAGE(nt < avg, "fast SHA blocks should harden difficulty");
    }
    // Too-slow SHA blocks -> next target should be easier (larger) than avg.
    {
        auto blocks = build(T * 3);
        const unsigned int next = GetNextWorkRequiredLwma(&blocks->at(total - 1), nullptr, /*sha=*/true, p);
        arith_uint256 nt; nt.SetCompact(next);
        BOOST_CHECK_MESSAGE(nt > avg, "slow SHA blocks should ease difficulty");
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // check max target * 4*nPowTargetTimespan doesn't overflow -- see pow.cpp:CalculateNextWorkRequired()
    if (!consensus.fPowNoRetargeting) {
        arith_uint256 targ_max{UintToArith256(uint256{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"})};
        targ_max /= consensus.nPowTargetTimespan*4;
        BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
    }
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET4_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET4);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

BOOST_AUTO_TEST_SUITE_END()
