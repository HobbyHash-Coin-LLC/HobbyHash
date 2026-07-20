// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#include <node/network_census.h>

namespace node {

void NetworkCensus::SubmitHeartbeat(int pool_id, const CensusPoolReport& report)
{
    LOCK(m_mutex);
    m_pools[pool_id] = report;
}

CensusSample NetworkCensus::ComputeReported(int64_t now, int64_t freshness_secs) const
{
    CensusSample s;
    s.time = now;
    for (const auto& [pool_id, r] : m_pools) {
        if (now - r.time > freshness_secs) continue; // stale pool, skip
        ++s.active_pools;

        s.sha.hashrate += r.sha.hashrate;
        s.sha.unique_miners += r.sha.unique_miners;
        s.sha.workers += r.sha.workers;

        s.kawpow.hashrate += r.kawpow.hashrate;
        s.kawpow.unique_miners += r.kawpow.unique_miners;
        s.kawpow.workers += r.kawpow.workers;

        s.randomx.hashrate += r.randomx.hashrate;
        s.randomx.unique_miners += r.randomx.unique_miners;
        s.randomx.workers += r.randomx.workers;
    }
    s.total.hashrate = s.sha.hashrate + s.kawpow.hashrate + s.randomx.hashrate;
    s.total.unique_miners = s.sha.unique_miners + s.kawpow.unique_miners + s.randomx.unique_miners;
    s.total.workers = s.sha.workers + s.kawpow.workers + s.randomx.workers;
    return s;
}

CensusSample NetworkCensus::CurrentReported(int64_t now, int64_t freshness_secs) const
{
    LOCK(m_mutex);
    return ComputeReported(now, freshness_secs);
}

bool NetworkCensus::MaybeSnapshot(int64_t now, double node_hash_sha, double node_hash_kawpow, double node_hash_randomx)
{
    LOCK(m_mutex);
    if (m_last_snapshot != 0 && now - m_last_snapshot < SNAPSHOT_INTERVAL_SECS) {
        return false;
    }
    CensusSample s = ComputeReported(now, DEFAULT_FRESHNESS_SECS);
    s.node_hash_sha = node_hash_sha;
    s.node_hash_kawpow = node_hash_kawpow;
    s.node_hash_randomx = node_hash_randomx;
    m_series.push_back(s);
    while (m_series.size() > MAX_SERIES) {
        m_series.pop_front();
    }
    m_last_snapshot = now;
    return true;
}

std::vector<CensusSample> NetworkCensus::Series(size_t max_samples) const
{
    LOCK(m_mutex);
    std::vector<CensusSample> out;
    size_t start = 0;
    if (max_samples != 0 && m_series.size() > max_samples) {
        start = m_series.size() - max_samples;
    }
    for (size_t i = start; i < m_series.size(); ++i) {
        out.push_back(m_series[i]);
    }
    return out;
}

std::map<int, CensusPoolReport> NetworkCensus::ActivePools(int64_t now, int64_t freshness_secs) const
{
    LOCK(m_mutex);
    std::map<int, CensusPoolReport> out;
    for (const auto& [pool_id, r] : m_pools) {
        if (now - r.time > freshness_secs) continue;
        out[pool_id] = r;
    }
    return out;
}

NetworkCensus& GetNetworkCensus()
{
    static NetworkCensus g_census;
    return g_census;
}

} // namespace node
