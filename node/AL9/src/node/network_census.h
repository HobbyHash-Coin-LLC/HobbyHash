// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#ifndef BITCOIN_NODE_NETWORK_CENSUS_H
#define BITCOIN_NODE_NETWORK_CENSUS_H

#include <sync.h>

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace node {

/** Per-algo slice of a pool heartbeat or an aggregate sample. */
struct CensusAlgoReport {
    double hashrate{0.0};       // hashes/sec reported (or node-estimated)
    int64_t unique_miners{0};   // distinct miner payout identities
    int64_t workers{0};         // distinct connected workers
};

/** A single pool's most recent authenticated heartbeat. */
struct CensusPoolReport {
    int64_t time{0};            // unix seconds of last heartbeat
    std::string pool_name;      // as shown on the explorer
    std::string pool_site;      // website or IP
    CensusAlgoReport sha;
    CensusAlgoReport kawpow;
    CensusAlgoReport randomx;
};

/** One minute of the rolling network census series. */
struct CensusSample {
    int64_t time{0};
    // Reported totals summed across pools that were fresh at snapshot time.
    CensusAlgoReport sha;
    CensusAlgoReport kawpow;
    CensusAlgoReport randomx;
    CensusAlgoReport total;
    int active_pools{0};
    // Node's own independent per-algo hashrate estimate (from the chain, not pool self-report).
    double node_hash_sha{0.0};
    double node_hash_kawpow{0.0};
    double node_hash_randomx{0.0};
};

/**
 * In-memory network census. Pools push authenticated per-minute heartbeats; the node keeps the
 * latest report per pool, sums fresh reports on demand, and snapshots a rolling 24h series
 * (one sample per minute) that also carries the node's own per-algo hashrate estimate.
 *
 * Memory-only: nothing is persisted to disk, so there is no on-disk format to migrate.
 */
class NetworkCensus
{
public:
    static constexpr int64_t DEFAULT_FRESHNESS_SECS = 150;   // a pool is "active" if seen in 2.5 min
    static constexpr int64_t SNAPSHOT_INTERVAL_SECS = 60;    // one series sample per minute
    static constexpr size_t MAX_SERIES = 1440;               // 24h at 1/min

    void SubmitHeartbeat(int pool_id, const CensusPoolReport& report);

    /** Append a snapshot to the series if >= SNAPSHOT_INTERVAL_SECS elapsed. Returns true if appended. */
    bool MaybeSnapshot(int64_t now, double node_hash_sha, double node_hash_kawpow, double node_hash_randomx);

    /** Live reported totals across pools fresh within freshness_secs. */
    CensusSample CurrentReported(int64_t now, int64_t freshness_secs = DEFAULT_FRESHNESS_SECS) const;

    /** Rolling series, oldest first, at most max_samples (0 = all). */
    std::vector<CensusSample> Series(size_t max_samples = 0) const;

    /** Fresh per-pool reports for detail display. */
    std::map<int, CensusPoolReport> ActivePools(int64_t now, int64_t freshness_secs = DEFAULT_FRESHNESS_SECS) const;

private:
    mutable Mutex m_mutex;
    std::map<int, CensusPoolReport> m_pools GUARDED_BY(m_mutex);
    std::deque<CensusSample> m_series GUARDED_BY(m_mutex);
    int64_t m_last_snapshot GUARDED_BY(m_mutex){0};

    CensusSample ComputeReported(int64_t now, int64_t freshness_secs) const EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
};

/** Process-wide census instance. */
NetworkCensus& GetNetworkCensus();

} // namespace node

#endif // BITCOIN_NODE_NETWORK_CENSUS_H
