// Copyright (c) 2026 The HobbyHash Core developers
// V6 multi-algo race consensus unit tests (Boost, part of test_bitcoin).

#include <consensus/amount.h>
#include <consensus/dual_pow.h>
#include <consensus/hashrate_subsidy.h>
#include <consensus/hobc_marker.h>
#include <consensus/params.h>
#include <node/network_census.h>
#include <primitives/block.h>
#include <streams.h>
#include <uint256.h>

#include <vector>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

// Self-contained consensus params for the race helpers (activation 200).
static Consensus::Params V6TestParams()
{
    Consensus::Params p;
    p.nSubsidyHalvingInterval = 840000;
    p.nHashrateSubsidyActivationHeight = 50;
    p.nPowPostForkTargetSpacing = 210;
    p.nPowTargetSpacing = 210;
    p.nSubsidyHashrateFallbackHobc = 45;
    p.nDualPowActivationHeight = 100;
    p.nDualPowCycleLength = 6;
    p.nDualPowShaCount = 3;
    p.nDualPowGpuSeedBits = 0x207fffff;
    p.nDualPowLwmaActivationHeight = 100;
    p.nLwmaWindow = 8;
    p.nDualPowLwmaGpuSeedBits = 0x207fffff;
    p.nMultiAlgoRaceActivationHeight = 200;
    p.nMultiAlgoPerAlgoTargetSpacing = 630;
    p.nMultiAlgoRandomXSeedBits = 0x207fffff;
    p.nMultiAlgoRaceReplayForkId = 0x00016800;
    p.powLimit = uint256{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    p.fPowNoRetargeting = true;
    return p;
}

BOOST_FIXTURE_TEST_SUITE(v6_multi_algo_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(race_activation_gate)
{
    const auto p = V6TestParams();
    BOOST_CHECK(!MultiAlgoRaceActive(199, p));
    BOOST_CHECK(MultiAlgoRaceActive(200, p));
    BOOST_CHECK(MultiAlgoRaceActive(16800, V6TestParams())); // sanity: gate is a >= compare
    // Pre-race 3/3 schedule unchanged (activation 100: mod0 SHA, mod3 GPU).
    BOOST_CHECK(IsShaBlockHeight(102, p));
    BOOST_CHECK(IsGpuBlockHeight(105, p));
}

BOOST_AUTO_TEST_CASE(algo_classification_race)
{
    const auto p = V6TestParams();
    CBlockHeader h;
    h.nVersion = 0x20000000;
    BOOST_CHECK(AlgoFromHeader(h, 200, p) == PowAlgo::SHA256);

    h.nVersion |= BLOCK_VERSION_GPU_KAWPOW;
    h.nNonce64 = 1;
    h.mixHash = uint256{"0100000000000000000000000000000000000000000000000000000000000000"};
    BOOST_CHECK(AlgoFromHeader(h, 200, p) == PowAlgo::KAWPOW);

    h.SetNull();
    h.nVersion = BLOCK_VERSION_CPU_RANDOMX;
    h.nNonce64 = 2;
    h.mixHash = uint256{"0200000000000000000000000000000000000000000000000000000000000000"};
    BOOST_CHECK(AlgoFromHeader(h, 200, p) == PowAlgo::RANDOMX);
}

// The 13190 class: below activation, the RandomX/KawPow version bits are IGNORED for algo
// identity (BIP320 miners legally roll them). Must classify as SHA — never RandomX/KawPow.
BOOST_AUTO_TEST_CASE(historical_rolled_bits_are_sha_below_activation)
{
    const auto p = V6TestParams();
    CBlockHeader h;
    // height 13190-style: bit 18 rolled on a pre-race SHA slot.
    h.nVersion = 0x20d50004; // bit18 set, bit17 clear (the real block 13190 nVersion)
    BOOST_CHECK(AlgoFromHeader(h, 150, p) == PowAlgo::SHA256);   // 150 < 200 activation, 150%6=0 SHA slot
    // Below activation, algo follows the SCHEDULE, not version bits: a rolled bit 17 on a
    // pre-race SHA slot (102%6=0) is still SHA (the bit does not flip it to KawPow).
    h.SetNull();
    h.nVersion = 0x20000000 | BLOCK_VERSION_GPU_KAWPOW;
    BOOST_CHECK(AlgoFromHeader(h, 102, p) == PowAlgo::SHA256);
}

// Direct guard for the index-load crash: a bit-18 SHA header below activation must
// (de)serialize as a legacy 80-byte header (4-byte nNonce, no nNonce64/mixHash).
BOOST_AUTO_TEST_CASE(header_serialize_roundtrip_bit18_below_activation)
{
    const auto p = V6TestParams();

    CBlockHeader sha;
    sha.nVersion = 0x20d50004; // bit18 rolled, SHA block
    sha.hashPrevBlock = uint256{"1111111111111111111111111111111111111111111111111111111111111111"};
    sha.hashMerkleRoot = uint256{"2222222222222222222222222222222222222222222222222222222222222222"};
    sha.nTime = 1717000000;
    sha.nBits = 0x1d00ffff;
    sha.nNonce = 0x12345678;
    sha.nNonce64 = 0;
    sha.mixHash.SetNull();

    // Serialize (write path): must emit legacy nNonce (4 bytes), NOT nNonce64+mixHash.
    DataStream ss;
    ss << sha;
    BOOST_CHECK_EQUAL(ss.size(), 80U); // 4+32+32+4+4+4

    // Deserialize below activation → 4-byte nNonce, fields stay empty, hash preserved.
    CBlockHeader out;
    {
        BlockHeaderUnserializeScope scope(150, p); // 150 < 200
        ss >> out;
    }
    BOOST_CHECK_EQUAL(out.nVersion, sha.nVersion);
    BOOST_CHECK_EQUAL(out.nNonce, sha.nNonce);
    BOOST_CHECK_EQUAL(out.nNonce64, 0U);
    BOOST_CHECK(out.mixHash.IsNull());
    BOOST_CHECK(out.GetHash() == sha.GetHash());
}

// At/after activation, a genuine RandomX header round-trips WITH the extended fields.
BOOST_AUTO_TEST_CASE(header_serialize_roundtrip_randomx_at_activation)
{
    const auto p = V6TestParams();

    CBlockHeader rx;
    // V6: a genuine RandomX header at/after activation carries the rolling-safe
    // BLOCK_VERSION_V6_EXTENDED bit (set by GBT) in addition to the RandomX algo bit.
    rx.nVersion = BLOCK_VERSION_CPU_RANDOMX | BLOCK_VERSION_V6_EXTENDED;
    rx.hashPrevBlock = uint256{"3333333333333333333333333333333333333333333333333333333333333333"};
    rx.hashMerkleRoot = uint256{"4444444444444444444444444444444444444444444444444444444444444444"};
    rx.nTime = 1717000123;
    rx.nBits = 0x207fffff;
    rx.nNonce = 0;
    rx.nNonce64 = 0xdeadbeefcafe;
    rx.mixHash = uint256{"5555555555555555555555555555555555555555555555555555555555555555"};

    DataStream ss;
    ss << rx;
    BOOST_CHECK_EQUAL(ss.size(), 80U + 8U + 32U - 4U); // legacy80 - nNonce(4) + nNonce64(8) + mixHash(32) = 116

    CBlockHeader out;
    {
        BlockHeaderUnserializeScope scope(250, p); // 250 >= 200
        ss >> out;
    }
    BOOST_CHECK_EQUAL(out.nNonce64, rx.nNonce64);
    BOOST_CHECK(out.mixHash == rx.mixHash);
}

// Core V6 fix: at/after activation a SHA block whose AsicBoost firmware version-rolled
// the algo-signal bits 17/18 (but never bit 0) must still deserialize as an 80-byte
// legacy header (4-byte nNonce, no ext fields) and classify as SHA — NOT be misread as a
// 116-byte RandomX/KawPow header. This is the wire/disk-length determinism guarantee.
BOOST_AUTO_TEST_CASE(rolled_sha_decodes_as_legacy_at_activation)
{
    const auto p = V6TestParams();

    CBlockHeader sha;
    // base 0x30000000 with bits 17 AND 18 rolled by the ASIC; bit 0 (EXTENDED) is clear.
    sha.nVersion = 0x30000000 | BLOCK_VERSION_GPU_KAWPOW | BLOCK_VERSION_CPU_RANDOMX;
    BOOST_CHECK_EQUAL((sha.nVersion & BLOCK_VERSION_V6_EXTENDED), 0);
    sha.hashPrevBlock = uint256{"6666666666666666666666666666666666666666666666666666666666666666"};
    sha.hashMerkleRoot = uint256{"7777777777777777777777777777777777777777777777777777777777777777"};
    sha.nTime = 1717000200;
    sha.nBits = 0x207fffff;
    sha.nNonce = 0x0badf00d;
    sha.nNonce64 = 0;
    sha.mixHash.SetNull();

    // Write path: no ext fields ⇒ legacy 80-byte header even with bits 17/18 set.
    DataStream ss;
    ss << sha;
    BOOST_CHECK_EQUAL(ss.size(), 80U);

    // Read path at/after activation: EXTENDED bit clear ⇒ 4-byte nNonce, no ext fields.
    CBlockHeader out;
    {
        BlockHeaderUnserializeScope scope(250, p); // 250 >= 200 activation
        ss >> out;
    }
    BOOST_CHECK_EQUAL(out.nVersion, sha.nVersion);
    BOOST_CHECK_EQUAL(out.nNonce, sha.nNonce);
    BOOST_CHECK_EQUAL(out.nNonce64, 0U);
    BOOST_CHECK(out.mixHash.IsNull());
    BOOST_CHECK(out.GetHash() == sha.GetHash());

    // Classification: rolled bits 17/18 with no ext fields ⇒ SHA (dual-binds to a SHA marker).
    BOOST_CHECK(AlgoFromHeader(out, 250, p) == PowAlgo::SHA256);
}

// A rolled-bit SHA header must classify as SHA even before deserialization normalizes it:
// AlgoFromHeader keys on field presence, not the (rolled) algo-signal bits.
BOOST_AUTO_TEST_CASE(rolled_sha_classifies_as_sha_at_activation)
{
    const auto p = V6TestParams();
    CBlockHeader h;
    h.nVersion = 0x30000000 | BLOCK_VERSION_CPU_RANDOMX; // bit 18 rolled, no ext fields
    BOOST_CHECK(AlgoFromHeader(h, 250, p) == PowAlgo::SHA256);
    h.nVersion = 0x30000000 | BLOCK_VERSION_GPU_KAWPOW;  // bit 17 rolled, no ext fields
    BOOST_CHECK(AlgoFromHeader(h, 250, p) == PowAlgo::SHA256);
}

BOOST_AUTO_TEST_CASE(flat_subsidy_and_names)
{
    const auto p = V6TestParams();
    BOOST_CHECK_EQUAL(Consensus::GetBlockSubsidy(200, p, nullptr), 45 * COIN);
    BOOST_CHECK_EQUAL(Consensus::GetBlockSubsidy(105, p, nullptr), 45 * COIN);

    BOOST_CHECK(std::string(PowAlgoName(PowAlgo::SHA256)) == "sha256");
    BOOST_CHECK(std::string(PowAlgoName(PowAlgo::KAWPOW)) == "kawpow");
    BOOST_CHECK(std::string(PowAlgoName(PowAlgo::RANDOMX)) == "randomx");
}

BOOST_AUTO_TEST_CASE(replay_fork_at_activation)
{
    const auto p = V6TestParams();
    const auto fork = ReplayForkForHeight(200, p);
    BOOST_CHECK(fork.require_sighash);
    BOOST_CHECK_EQUAL(fork.fork_id, 0x00016800U);
}

// ---- HOBC coinbase telemetry marker (consensus-required at/after activation) --------------

BOOST_AUTO_TEST_CASE(hobc_marker_roundtrip_required_only)
{
    HobcTelemetryMarker in;
    in.pool_id = 7;
    in.algo = 2; // randomx
    in.winning_share_diff = 123456.5f;

    const auto bytes = BuildHobcTelemetryMarker(in);
    // magic(4)+ver(1)+pool(3)+algo(3)+wdiff(6) = 17 bytes when no optional fields.
    BOOST_CHECK_EQUAL(bytes.size(), 17U);

    HobcTelemetryMarker out;
    BOOST_CHECK(ParseHobcTelemetryMarker(bytes, out));
    BOOST_CHECK_EQUAL(out.pool_id, 7);
    BOOST_CHECK_EQUAL(out.algo, 2);
    BOOST_CHECK_EQUAL(out.winning_share_diff, 123456.5f);
    BOOST_CHECK(out.pool_site.empty());
    BOOST_CHECK(out.rig_model.empty());
    BOOST_CHECK(out.worker_name.empty());
}

BOOST_AUTO_TEST_CASE(hobc_marker_roundtrip_all_fields)
{
    HobcTelemetryMarker in;
    in.pool_id = 1;
    in.algo = 0; // sha256
    in.winning_share_diff = 42.0f;
    in.pool_site = "hobbyhashcoin.com";
    in.rig_model = "S19 XP";
    in.worker_name = "rig01";

    const auto bytes = BuildHobcTelemetryMarker(in);
    HobcTelemetryMarker out;
    BOOST_CHECK(ParseHobcTelemetryMarker(bytes, out));
    BOOST_CHECK_EQUAL(out.pool_id, 1);
    BOOST_CHECK_EQUAL(out.algo, 0);
    BOOST_CHECK_EQUAL(out.winning_share_diff, 42.0f);
    BOOST_CHECK_EQUAL(out.pool_site, std::string("hobbyhashcoin.com"));
    BOOST_CHECK_EQUAL(out.rig_model, std::string("S19 XP"));
    BOOST_CHECK_EQUAL(out.worker_name, std::string("rig01"));
    // Real-world budget sanity: full marker well under the 100-byte coinbase-sig budget.
    BOOST_CHECK(bytes.size() <= 100U);
}

// Marker embedded inside a realistic coinbase scriptSig: [height push][extranonce]...[marker]...[tail]
BOOST_AUTO_TEST_CASE(hobc_marker_found_when_embedded)
{
    HobcTelemetryMarker in;
    in.pool_id = 3;
    in.algo = 1; // kawpow
    in.winning_share_diff = 999.0f;
    in.pool_site = "pool.example";
    const auto marker = BuildHobcTelemetryMarker(in);

    std::vector<unsigned char> cb;
    // fake serialized-height push + extranonce noise before the marker
    const unsigned char prefix[] = {0x03, 0xb8, 0x41, 0x00, 0xde, 0xad, 0xbe, 0xef};
    cb.insert(cb.end(), prefix, prefix + sizeof(prefix));
    cb.insert(cb.end(), marker.begin(), marker.end());
    const unsigned char tail[] = {0x00, 0x00, 0x11, 0x22};
    cb.insert(cb.end(), tail, tail + sizeof(tail));

    HobcTelemetryMarker out;
    BOOST_CHECK(ParseHobcTelemetryMarker(cb, out));
    BOOST_CHECK_EQUAL(out.pool_id, 3);
    BOOST_CHECK_EQUAL(out.algo, 1);
    BOOST_CHECK_EQUAL(out.pool_site, std::string("pool.example"));
}

BOOST_AUTO_TEST_CASE(hobc_marker_rejects_malformed)
{
    HobcTelemetryMarker out;

    // No magic at all.
    std::vector<unsigned char> none = {0x03, 0xb8, 0x41, 0x00, 0x00, 0x00};
    BOOST_CHECK(!ParseHobcTelemetryMarker(none, out));

    // Magic + bad version.
    std::vector<unsigned char> badver = {'H', 'O', 'B', 'C', 0x02, 0x01, 0x01, 0x00};
    BOOST_CHECK(!ParseHobcTelemetryMarker(badver, out));

    // Magic + version but truncated before required TLVs complete.
    HobcTelemetryMarker in;
    in.pool_id = 1; in.algo = 0; in.winning_share_diff = 1.0f;
    auto trunc = BuildHobcTelemetryMarker(in);
    trunc.resize(trunc.size() - 2); // chop into the winning_diff value
    BOOST_CHECK(!ParseHobcTelemetryMarker(trunc, out));

    // Magic + version + algo out of range (3).
    std::vector<unsigned char> badalgo = {
        'H', 'O', 'B', 'C', 0x01,
        0x01, 0x01, 0x05,          // pool_id=5
        0x02, 0x01, 0x03,          // algo=3 (invalid)
        0x03, 0x04, 0x00, 0x00, 0x00, 0x00};
    BOOST_CHECK(!ParseHobcTelemetryMarker(badalgo, out));
}

// ---- Network census (authenticated pool heartbeats + rolling series) ----------------------

BOOST_AUTO_TEST_CASE(census_aggregates_fresh_pools_and_drops_stale)
{
    node::NetworkCensus c;
    const int64_t now = 1'000'000;

    node::CensusPoolReport p1;
    p1.time = now; p1.pool_name = "main"; p1.sha = {1.0e15, 40, 120};
    c.SubmitHeartbeat(1, p1);

    node::CensusPoolReport p2;
    p2.time = now; p2.pool_name = "gpu"; p2.kawpow = {5.0e9, 12, 30};
    c.SubmitHeartbeat(2, p2);

    node::CensusPoolReport p3;
    p3.time = now; p3.pool_name = "cpu"; p3.randomx = {2.5e5, 8, 15};
    c.SubmitHeartbeat(3, p3);

    const auto rep = c.CurrentReported(now);
    BOOST_CHECK_EQUAL(rep.active_pools, 3);
    BOOST_CHECK_EQUAL(rep.sha.workers, 120);
    BOOST_CHECK_EQUAL(rep.kawpow.unique_miners, 12);
    BOOST_CHECK_EQUAL(rep.randomx.workers, 15);
    BOOST_CHECK_EQUAL(rep.total.unique_miners, 60);
    BOOST_CHECK_EQUAL(rep.total.workers, 165);
    BOOST_CHECK_CLOSE(rep.total.hashrate, 1.0e15 + 5.0e9 + 2.5e5, 1e-6);

    // A pool that last reported outside the freshness window is excluded.
    const int64_t later = now + node::NetworkCensus::DEFAULT_FRESHNESS_SECS + 10;
    const auto rep2 = c.CurrentReported(later);
    BOOST_CHECK_EQUAL(rep2.active_pools, 0);
    BOOST_CHECK_EQUAL(rep2.total.workers, 0);
}

BOOST_AUTO_TEST_CASE(census_snapshot_interval_and_series_cap)
{
    node::NetworkCensus c;
    int64_t t = 2'000'000;

    node::CensusPoolReport p;
    p.time = t; p.sha = {1.0e15, 10, 20};
    c.SubmitHeartbeat(1, p);

    // First snapshot always appends.
    BOOST_CHECK(c.MaybeSnapshot(t, 1.0, 2.0, 3.0));
    // Within the interval → no new sample.
    BOOST_CHECK(!c.MaybeSnapshot(t + 5, 1.0, 2.0, 3.0));
    // After the interval → appends.
    BOOST_CHECK(c.MaybeSnapshot(t + node::NetworkCensus::SNAPSHOT_INTERVAL_SECS, 1.0, 2.0, 3.0));

    const auto series = c.Series(0);
    BOOST_CHECK_EQUAL(series.size(), 2U);
    BOOST_CHECK_EQUAL(series.back().node_hash_kawpow, 2.0);
    BOOST_CHECK_EQUAL(series.back().node_hash_randomx, 3.0);
}

BOOST_AUTO_TEST_SUITE_END()
