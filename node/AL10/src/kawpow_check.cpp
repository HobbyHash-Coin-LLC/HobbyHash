// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#include <kawpow_check.h>

#include <arith_uint256.h>
#include <consensus/dual_pow.h>
#include <consensus/params.h>
#include <pow.h>
#include <primitives/block.h>
#include <uint256.h>

#include <ethash/ethash.hpp>
#include <ethash/keccak.hpp>
#include <ethash/progpow.hpp>

#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace {

void AppendLE32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

ethash::hash256 ToEthashHash(const uint256& h)
{
    ethash::hash256 out{};
    std::memcpy(out.bytes, h.begin(), 32);
    return out;
}

bool NBitsToBoundary(unsigned int nBits, const uint256& pow_limit, ethash::hash256& boundary_out)
{
    const auto target = DeriveTarget(nBits, pow_limit);
    if (!target) return false;
    const uint256 target256 = ArithToUint256(*target);
    // Match GBT/RPC target hex byte order (display order), not uint256 internal layout.
    for (size_t i = 0; i < 32; ++i) {
        boundary_out.bytes[i] = target256.begin()[31 - i];
    }
    return true;
}

const ethash::epoch_context& GetEpochContext(int height)
{
    // Use the library's globally-cached epoch context instead of
    // create_epoch_context(), which rebuilds the ethash light cache from scratch
    // on every call (~hundreds of ms). Full KawPow verify is used when accepting
    // new blocks; LoadBlockIndexGuts uses CheckBlockProofOfWorkForIndexLoad so
    // cold starts do not re-run progpow for every historical GPU header.
    // get_global_epoch_context builds the cache once per epoch and reuses it.
    const int epoch = ethash::get_epoch_number(height);
    return ethash::get_global_epoch_context(epoch);
}

} // namespace

uint256 KawPowHeaderHash(const CBlockHeader& block, int height)
{
    std::vector<uint8_t> data;
    data.reserve(80);
    AppendLE32(data, static_cast<uint32_t>(block.nVersion));
    data.insert(data.end(), block.hashPrevBlock.begin(), block.hashPrevBlock.end());
    data.insert(data.end(), block.hashMerkleRoot.begin(), block.hashMerkleRoot.end());
    AppendLE32(data, block.nTime);
    AppendLE32(data, block.nBits);
    AppendLE32(data, static_cast<uint32_t>(height));

    ethash::hash256 out = ethash::keccak256(data.data(), data.size());
    return uint256(std::span<const unsigned char>(out.bytes, out.bytes + 32));
}

bool CheckKawPowProofOfWork(const CBlockHeader& block, int height, const Consensus::Params& params)
{
    if (!(block.nVersion & BLOCK_VERSION_GPU_KAWPOW)) return false;
    if (block.mixHash.IsNull()) return false;

    ethash::hash256 boundary{};
    if (!NBitsToBoundary(block.nBits, params.powLimit, boundary)) return false;

    const ethash::hash256 header_hash = ToEthashHash(KawPowHeaderHash(block, height));
    const ethash::hash256 mix_hash = ToEthashHash(block.mixHash);
    const ethash::epoch_context& ctx = GetEpochContext(height);

    return progpow::verify(ctx, height, header_hash, mix_hash, block.nNonce64, boundary);
}
