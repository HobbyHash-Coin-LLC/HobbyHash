// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#include <randomx_check.h>

#include <arith_uint256.h>
#include <consensus/dual_pow.h>
#include <consensus/params.h>
#include <pow.h>
#include <primitives/block.h>
#include <uint256.h>

#include <cstring>
#include <mutex>
#include <vector>

extern "C" {
#include <randomx.h>
}

namespace {

constexpr int RANDOMX_EPOCH_BLOCKS = 2048;

std::mutex g_rx_mutex;
randomx_cache* g_rx_cache = nullptr;
randomx_vm* g_rx_vm = nullptr;
int g_rx_epoch = -1;

void AppendLE32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

void AppendLE64(std::vector<uint8_t>& out, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
}

/** Seed key for RandomX cache: "HOBCRX01" || epoch_le32 */
std::vector<uint8_t> RandomXSeedKey(int height)
{
    const uint32_t epoch = static_cast<uint32_t>(height / RANDOMX_EPOCH_BLOCKS);
    std::vector<uint8_t> key = {'H', 'O', 'B', 'C', 'R', 'X', '0', '1'};
    AppendLE32(key, epoch);
    return key;
}

/** Preimage hashed by RandomX: version|prev|merkle|time|bits|height|nonce64 (LE). */
std::vector<uint8_t> RandomXHeaderBlob(const CBlockHeader& block, int height)
{
    std::vector<uint8_t> data;
    data.reserve(88);
    AppendLE32(data, static_cast<uint32_t>(block.nVersion));
    data.insert(data.end(), block.hashPrevBlock.begin(), block.hashPrevBlock.end());
    data.insert(data.end(), block.hashMerkleRoot.begin(), block.hashMerkleRoot.end());
    AppendLE32(data, block.nTime);
    AppendLE32(data, block.nBits);
    AppendLE32(data, static_cast<uint32_t>(height));
    AppendLE64(data, block.nNonce64);
    return data;
}

bool EnsureVm(int height)
{
    const int epoch = height / RANDOMX_EPOCH_BLOCKS;
    if (g_rx_vm && g_rx_epoch == epoch) return true;

    if (g_rx_vm) {
        randomx_destroy_vm(g_rx_vm);
        g_rx_vm = nullptr;
    }
    if (g_rx_cache) {
        randomx_release_cache(g_rx_cache);
        g_rx_cache = nullptr;
    }

    const randomx_flags flags = randomx_get_flags();
    g_rx_cache = randomx_alloc_cache(flags);
    if (!g_rx_cache) return false;
    const auto key = RandomXSeedKey(height);
    randomx_init_cache(g_rx_cache, key.data(), key.size());
    g_rx_vm = randomx_create_vm(flags, g_rx_cache, nullptr);
    if (!g_rx_vm) return false;
    g_rx_epoch = epoch;
    return true;
}

} // namespace

bool ComputeRandomXHash(const CBlockHeader& block, int height, unsigned char* out32)
{
    std::lock_guard<std::mutex> lock(g_rx_mutex);
    if (!EnsureVm(height)) return false;
    const auto blob = RandomXHeaderBlob(block, height);
    randomx_calculate_hash(g_rx_vm, blob.data(), blob.size(), out32);
    return true;
}

bool CheckRandomXProofOfWork(const CBlockHeader& block, int height, const Consensus::Params& params)
{
    if (!(block.nVersion & BLOCK_VERSION_CPU_RANDOMX)) return false;
    if (block.nVersion & BLOCK_VERSION_GPU_KAWPOW) return false;
    if (block.mixHash.IsNull()) return false;

    unsigned char digest[RANDOMX_HASH_SIZE];
    if (!ComputeRandomXHash(block, height, digest)) return false;

    uint256 hash;
    std::memcpy(hash.begin(), digest, 32);
    if (hash != block.mixHash) return false;

    // Validate against the height-gated RandomX pow limit (eased at/after
    // nRandomXEaseActivationHeight) so CPU-solvable easy RandomX targets are accepted.
    // Below the ease height this resolves to the shared powLimit (unchanged behavior).
    const auto bnTarget = DeriveTarget(block.nBits, RandomXPowLimit(height, params));
    if (!bnTarget) return false;
    return UintToArith256(hash) <= *bnTarget;
}
