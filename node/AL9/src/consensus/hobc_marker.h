// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#ifndef BITCOIN_CONSENSUS_HOBC_MARKER_H
#define BITCOIN_CONSENSUS_HOBC_MARKER_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/**
 * HOBC V6 coinbase telemetry marker (see docs/FORK_SPEC_V6.md).
 *
 * Compact TLV embedded in the coinbase scriptSig. At/after the multi-algo race
 * activation height the node REQUIRES a well-formed marker in every block's coinbase
 * (format-required enforcement) and requires marker.algo == header-derived algo.
 * Below activation the coinbase is never inspected for a marker.
 *
 * Wire layout: magic "HOBC" (4) + version (1) + canonical-order TLVs [type(1) len(1) value]:
 *   0x01 pool_id            len 1   REQUIRED
 *   0x02 algo               len 1   REQUIRED  (0=sha256 1=kawpow 2=randomx)
 *   0x03 winning_share_diff len 4   REQUIRED  (IEEE-754 float32, little-endian)
 *   0x04 pool_site          len<=24 optional
 *   0x05 rig_model          len<=16 optional
 *   0x06 worker_name        len<=16 optional
 */
struct HobcTelemetryMarker {
    static constexpr uint8_t VERSION = 1;

    uint8_t pool_id{0};
    uint8_t algo{0};
    float winning_share_diff{0.0f};
    std::string pool_site;
    std::string rig_model;
    std::string worker_name;

    static constexpr size_t MAX_POOL_SITE = 24;
    static constexpr size_t MAX_RIG_MODEL = 16;
    static constexpr size_t MAX_WORKER = 16;

    // TLV type tags
    static constexpr uint8_t T_POOL_ID = 0x01;
    static constexpr uint8_t T_ALGO    = 0x02;
    static constexpr uint8_t T_WDIFF   = 0x03;
    static constexpr uint8_t T_SITE    = 0x04;
    static constexpr uint8_t T_RIG     = 0x05;
    static constexpr uint8_t T_WORKER  = 0x06;
};

/**
 * Parse the first well-formed HOBC marker found in the raw coinbase scriptSig bytes.
 * Returns true iff magic + version==1 + required TLVs (pool_id, algo in {0,1,2},
 * winning_share_diff) are present and well-formed. Deterministic: scans left-to-right and
 * returns the first offset whose canonical-order parse succeeds.
 */
bool ParseHobcTelemetryMarker(std::span<const unsigned char> coinbase_script, HobcTelemetryMarker& out);

/**
 * Serialize a marker to bytes (magic + version + canonical-order TLVs). Optional text fields
 * are emitted only when non-empty and are truncated to their caps.
 */
std::vector<unsigned char> BuildHobcTelemetryMarker(const HobcTelemetryMarker& m);

#endif // BITCOIN_CONSENSUS_HOBC_MARKER_H
