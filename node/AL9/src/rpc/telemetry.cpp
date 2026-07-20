// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <chain.h>
#include <chainparams.h>
#include <common/args.h>
#include <consensus/dual_pow.h>
#include <consensus/params.h>
#include <node/context.h>
#include <node/network_census.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <univalue.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>
#include <cstring>

using node::CensusAlgoReport;
using node::CensusPoolReport;
using node::CensusSample;
using node::GetNetworkCensus;
using node::NetworkCensus;

// Constant-time-ish equality to avoid trivially leaking the census token via early-out timing.
static bool TokenEqual(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

// Node's own independent per-algo hashrate estimate: sum each algo's per-block work over the last
// `lookup` blocks and divide by the wall-clock span of that window. Height-gated algo classification
// (AlgoAtIndex) means historical blocks are never misclassified by rolled version bits.
static void PerAlgoNodeHashPS(const CChain& chain, const Consensus::Params& params, int lookup,
                              double& sha, double& kawpow, double& randomx)
{
    sha = kawpow = randomx = 0.0;
    const CBlockIndex* tip = chain.Tip();
    if (tip == nullptr || tip->nHeight == 0) return;
    if (lookup <= 0) lookup = 120;
    if (lookup > tip->nHeight) lookup = tip->nHeight;

    arith_uint256 work_sha, work_kaw, work_rx;
    const CBlockIndex* pb = tip;
    int64_t minTime = pb->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup && pb != nullptr; ++i) {
        const PowAlgo a = AlgoAtIndex(pb, params);
        const arith_uint256 w = GetBlockProof(*pb);
        if (a == PowAlgo::SHA256) work_sha += w;
        else if (a == PowAlgo::KAWPOW) work_kaw += w;
        else if (a == PowAlgo::RANDOMX) work_rx += w;
        const int64_t t = pb->GetBlockTime();
        minTime = std::min(minTime, t);
        maxTime = std::max(maxTime, t);
        pb = pb->pprev;
    }
    const int64_t timeDiff = maxTime - minTime;
    if (timeDiff <= 0) return;
    sha = work_sha.getdouble() / timeDiff;
    kawpow = work_kaw.getdouble() / timeDiff;
    randomx = work_rx.getdouble() / timeDiff;
}

static CensusAlgoReport ParseAlgoReport(const UniValue& o)
{
    CensusAlgoReport r;
    if (!o.isObject()) return r;
    if (const UniValue& v = o.find_value("hashrate"); !v.isNull()) r.hashrate = v.get_real();
    if (const UniValue& v = o.find_value("unique_miners"); !v.isNull()) r.unique_miners = v.getInt<int64_t>();
    if (const UniValue& v = o.find_value("workers"); !v.isNull()) r.workers = v.getInt<int64_t>();
    return r;
}

static UniValue AlgoReportToUni(const CensusAlgoReport& r)
{
    UniValue o(UniValue::VOBJ);
    o.pushKV("hashrate", r.hashrate);
    o.pushKV("unique_miners", r.unique_miners);
    o.pushKV("workers", r.workers);
    return o;
}

static RPCHelpMan submitcensus()
{
    return RPCHelpMan{
        "submitcensus",
        "Authenticated per-minute mining census heartbeat pushed by a HOBC-compliant pool.\n"
        "Requires the shared token configured on the node via -censustoken. Reports the pool's\n"
        "current per-algo hashrate, unique miners and worker counts. Worker IPs are NOT sent here;\n"
        "they are reported off-chain to the site database.\n",
        {
            {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Shared census token (must match node -censustoken)"},
            {"pool_id", RPCArg::Type::NUM, RPCArg::Optional::NO, "Numeric pool identifier (matches the coinbase marker pool-id)"},
            {"report", RPCArg::Type::OBJ, RPCArg::Optional::NO, "The heartbeat payload",
                {
                    {"pool_name", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Pool name as shown on the explorer"},
                    {"pool_site", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Pool website or IP"},
                    {"sha256", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "SHA256d stats",
                        {
                            {"hashrate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "hashes/sec"},
                            {"unique_miners", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "distinct miners"},
                            {"workers", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "distinct workers"},
                        }},
                    {"kawpow", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "KawPow stats",
                        {
                            {"hashrate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "hashes/sec"},
                            {"unique_miners", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "distinct miners"},
                            {"workers", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "distinct workers"},
                        }},
                    {"randomx", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "RandomX stats",
                        {
                            {"hashrate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "hashes/sec"},
                            {"unique_miners", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "distinct miners"},
                            {"workers", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "distinct workers"},
                        }},
                }},
        },
        RPCResult{RPCResult::Type::OBJ, "", "", {
            {RPCResult::Type::BOOL, "accepted", "Whether the heartbeat was accepted"},
            {RPCResult::Type::NUM, "pool_id", "The pool id recorded"},
            {RPCResult::Type::NUM_TIME, "time", "Unix time the heartbeat was recorded"},
        }},
        RPCExamples{
            HelpExampleCli("submitcensus", "\"mytoken\" 1 '{\"pool_name\":\"main\",\"sha256\":{\"hashrate\":1.2e15,\"unique_miners\":40,\"workers\":120}}'")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const ArgsManager& args{EnsureAnyArgsman(request.context)};
    const std::string configured{args.GetArg("-censustoken", "")};
    if (configured.empty()) {
        throw JSONRPCError(RPC_MISC_ERROR, "census disabled: node has no -censustoken configured");
    }
    const std::string token{request.params[0].get_str()};
    if (!TokenEqual(token, configured)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "census token rejected");
    }

    const int pool_id{request.params[1].getInt<int>()};
    const UniValue& rep{request.params[2].get_obj()};

    CensusPoolReport report;
    report.time = GetTime();
    if (const UniValue& v = rep.find_value("pool_name"); !v.isNull()) report.pool_name = v.get_str();
    if (const UniValue& v = rep.find_value("pool_site"); !v.isNull()) report.pool_site = v.get_str();
    report.sha = ParseAlgoReport(rep.find_value("sha256"));
    report.kawpow = ParseAlgoReport(rep.find_value("kawpow"));
    report.randomx = ParseAlgoReport(rep.find_value("randomx"));

    GetNetworkCensus().SubmitHeartbeat(pool_id, report);

    UniValue result(UniValue::VOBJ);
    result.pushKV("accepted", true);
    result.pushKV("pool_id", pool_id);
    result.pushKV("time", report.time);
    return result;
},
    };
}

static RPCHelpMan getnetworkstats()
{
    return RPCHelpMan{
        "getnetworkstats",
        "Live per-algo network mining census: reported totals (summed across compliant pools),\n"
        "the node's own independent per-algo hashrate estimate, active pool detail, and a rolling\n"
        "per-minute time series (24h max). Designed to be polled for a live feed.\n",
        {
            {"nblocks", RPCArg::Type::NUM, RPCArg::Default{120}, "Blocks to use for the node hashrate estimate window"},
            {"max_samples", RPCArg::Type::NUM, RPCArg::Default{60}, "Max rolling series samples to return (0 = all)"},
        },
        RPCResult{RPCResult::Type::OBJ, "", "", {
            {RPCResult::Type::NUM_TIME, "time", "Unix time of this response"},
            {RPCResult::Type::BOOL, "race_active", "Whether the multi-algo race is active at the current tip"},
            {RPCResult::Type::OBJ_DYN, "reported", "Totals summed across pools fresh within the freshness window", {
                {RPCResult::Type::ANY, "", ""},
            }},
            {RPCResult::Type::OBJ_DYN, "node_estimate", "Node's own per-algo hashrate estimate (hashes/sec)", {
                {RPCResult::Type::ANY, "", ""},
            }},
            {RPCResult::Type::ARR, "pools", "Active pool detail", {
                {RPCResult::Type::ANY, "", ""},
            }},
            {RPCResult::Type::ARR, "series", "Rolling per-minute samples (oldest first)", {
                {RPCResult::Type::ANY, "", ""},
            }},
        }},
        RPCExamples{
            HelpExampleCli("getnetworkstats", "")
            + HelpExampleRpc("getnetworkstats", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    ChainstateManager& chainman = EnsureAnyChainman(request.context);
    const Consensus::Params& params = chainman.GetConsensus();

    int nblocks = request.params[0].isNull() ? 120 : request.params[0].getInt<int>();
    size_t max_samples = request.params[1].isNull() ? 60 : (size_t)request.params[1].getInt<int>();

    double h_sha = 0, h_kaw = 0, h_rx = 0;
    int tip_height = 0;
    {
        LOCK(cs_main);
        const CChain& chain = chainman.ActiveChain();
        tip_height = chain.Height();
        PerAlgoNodeHashPS(chain, params, nblocks, h_sha, h_kaw, h_rx);
    }

    NetworkCensus& census = GetNetworkCensus();
    const int64_t now = GetTime();
    census.MaybeSnapshot(now, h_sha, h_kaw, h_rx);

    const CensusSample reported = census.CurrentReported(now);

    UniValue result(UniValue::VOBJ);
    result.pushKV("time", now);
    result.pushKV("race_active", MultiAlgoRaceActive(tip_height, params));

    UniValue rep(UniValue::VOBJ);
    rep.pushKV("sha256", AlgoReportToUni(reported.sha));
    rep.pushKV("kawpow", AlgoReportToUni(reported.kawpow));
    rep.pushKV("randomx", AlgoReportToUni(reported.randomx));
    rep.pushKV("total", AlgoReportToUni(reported.total));
    rep.pushKV("active_pools", reported.active_pools);
    result.pushKV("reported", std::move(rep));

    UniValue est(UniValue::VOBJ);
    est.pushKV("sha256_hashps", h_sha);
    est.pushKV("kawpow_hashps", h_kaw);
    est.pushKV("randomx_hashps", h_rx);
    est.pushKV("total_hashps", h_sha + h_kaw + h_rx);
    result.pushKV("node_estimate", std::move(est));

    UniValue pools(UniValue::VARR);
    for (const auto& [pool_id, r] : census.ActivePools(now)) {
        UniValue p(UniValue::VOBJ);
        p.pushKV("pool_id", pool_id);
        if (!r.pool_name.empty()) p.pushKV("pool_name", r.pool_name);
        if (!r.pool_site.empty()) p.pushKV("pool_site", r.pool_site);
        p.pushKV("last_seen", r.time);
        p.pushKV("sha256", AlgoReportToUni(r.sha));
        p.pushKV("kawpow", AlgoReportToUni(r.kawpow));
        p.pushKV("randomx", AlgoReportToUni(r.randomx));
        pools.push_back(std::move(p));
    }
    result.pushKV("pools", std::move(pools));

    UniValue series(UniValue::VARR);
    for (const CensusSample& s : census.Series(max_samples)) {
        UniValue o(UniValue::VOBJ);
        o.pushKV("time", s.time);
        o.pushKV("active_pools", s.active_pools);
        o.pushKV("reported_sha256", AlgoReportToUni(s.sha));
        o.pushKV("reported_kawpow", AlgoReportToUni(s.kawpow));
        o.pushKV("reported_randomx", AlgoReportToUni(s.randomx));
        o.pushKV("reported_total", AlgoReportToUni(s.total));
        o.pushKV("node_hash_sha256", s.node_hash_sha);
        o.pushKV("node_hash_kawpow", s.node_hash_kawpow);
        o.pushKV("node_hash_randomx", s.node_hash_randomx);
        series.push_back(std::move(o));
    }
    result.pushKV("series", std::move(series));

    return result;
},
    };
}

void RegisterTelemetryRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"mining", &submitcensus},
        {"mining", &getnetworkstats},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
