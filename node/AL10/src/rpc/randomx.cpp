// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/dual_pow.h>
#include <consensus/params.h>
#include <node/context.h>
#include <pow.h>
#include <primitives/block.h>
#include <randomx_check.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <uint256.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// getrandomxhash: compute the HOBC RandomX PoW digest for a candidate header, using the exact same
// hasher (ComputeRandomXHash) and seed-epoch logic the consensus rule uses. This lets the RandomX
// CPU stratum pool verify miner shares (and full-block solves) without duplicating RandomX or
// desynchronising from consensus. The returned "hash" is also the value the miner must place in the
// header mixHash for CheckRandomXProofOfWork to accept the block.
static RPCHelpMan getrandomxhash()
{
    return RPCHelpMan{
        "getrandomxhash",
        "\nCompute the RandomX PoW digest for the given HOBC header fields at a height.\n"
        "Inputs mirror the RandomX preimage: version|prev|merkle|time|bits|height|nonce64.\n"
        "The pool compares the returned hash against its share target and the block target (bits).\n",
        {
            {"version", RPCArg::Type::NUM, RPCArg::Optional::NO, "Block header nVersion (must have the RandomX bit set for a real solve)"},
            {"previousblockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Previous block hash (RPC/display hex)"},
            {"merkleroot", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Merkle root of the candidate block (RPC/display hex)"},
            {"time", RPCArg::Type::NUM, RPCArg::Optional::NO, "Header nTime"},
            {"bits", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Compact target bits (e.g. 207fffff)"},
            {"height", RPCArg::Type::NUM, RPCArg::Optional::NO, "Block height (selects the RandomX seed epoch)"},
            {"nonce64", RPCArg::Type::STR, RPCArg::Optional::NO, "64-bit nonce as a decimal string"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "hash", "the RandomX digest (also the required mixHash)"},
                {RPCResult::Type::BOOL, "meets_bits", "true if hash satisfies the block target implied by bits"},
                {RPCResult::Type::STR_HEX, "bits", "the compact bits used"},
            }},
        RPCExamples{
            HelpExampleCli("getrandomxhash", "805568512 \"<prev>\" \"<merkle>\" 1700000000 207fffff 200 \"12345\"")
            + HelpExampleRpc("getrandomxhash", "805568512, \"<prev>\", \"<merkle>\", 1700000000, \"207fffff\", 200, \"12345\"")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue {
            CBlockHeader header;
            header.nVersion = request.params[0].getInt<int>();
            header.hashPrevBlock = ParseHashV(request.params[1], "previousblockhash");
            header.hashMerkleRoot = ParseHashV(request.params[2], "merkleroot");
            header.nTime = static_cast<uint32_t>(request.params[3].getInt<int64_t>());

            const std::string bits_hex = request.params[4].get_str();
            uint32_t bits = 0;
            {
                std::string b = bits_hex;
                if (b.rfind("0x", 0) == 0 || b.rfind("0X", 0) == 0) b = b.substr(2);
                if (b.empty() || b.size() > 8 || !std::all_of(b.begin(), b.end(), [](unsigned char c) { return std::isxdigit(c); })) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "bits must be 1-8 hex digits");
                }
                bits = static_cast<uint32_t>(strtoul(b.c_str(), nullptr, 16));
            }
            header.nBits = bits;

            const int height = request.params[5].getInt<int>();
            if (height < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "height must be >= 0");

            const std::string nonce_str = request.params[6].get_str();
            {
                if (nonce_str.empty() || !std::all_of(nonce_str.begin(), nonce_str.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "nonce64 must be a non-negative decimal string");
                }
                errno = 0;
                header.nNonce64 = strtoull(nonce_str.c_str(), nullptr, 10);
            }

            unsigned char digest[32];
            if (!ComputeRandomXHash(header, height, digest)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "RandomX VM unavailable");
            }
            uint256 hash;
            std::memcpy(hash.begin(), digest, 32);

            const Consensus::Params& params = Params().GetConsensus();
            UniValue result(UniValue::VOBJ);
            result.pushKV("hash", hash.GetHex());
            // Use the height-gated RandomX pow limit so eased (post-ease-activation) block
            // targets are recognized as solves; below the ease height this equals powLimit.
            const auto bnTarget = DeriveTarget(bits, RandomXPowLimit(height, params));
            const bool meets_bits = bnTarget && (UintToArith256(hash) <= *bnTarget);
            result.pushKV("meets_bits", meets_bits);
            result.pushKV("bits", strprintf("%08x", bits));
            return result;
        },
    };
}

void RegisterRandomXRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"mining", &getrandomxhash},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
