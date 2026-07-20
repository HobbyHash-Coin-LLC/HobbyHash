// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <consensus/dual_pow.h>
#include <consensus/params.h>
#include <hash.h>
#include <kernel/chainparams.h>
#include <streams.h>
#include <tinyformat.h>

#include <memory>
#include <span>
#include <sstream>

namespace {

bool DeserializeLegacyShaBlockFromBytes(CBlock& block, std::span<const std::byte> data, int32_t& stored_version_out)
{
    block.SetNull();
    stored_version_out = 0;
    if (data.size() < 80) {
        return false;
    }

    std::vector<std::byte> payload(data.begin(), data.end());
    stored_version_out = ReadLE32(reinterpret_cast<const unsigned char*>(payload.data()));
    const int32_t on_disk_version = stored_version_out;
    if (stored_version_out & BLOCK_VERSION_GPU_KAWPOW) {
        WriteLE32(reinterpret_cast<unsigned char*>(payload.data()),
                  stored_version_out & ~BLOCK_VERSION_GPU_KAWPOW);
    }

    DataStream stream;
    stream.write({payload.data(), payload.size()});
    stream >> block.nVersion;
    stream >> block.hashPrevBlock;
    stream >> block.hashMerkleRoot;
    stream >> block.nTime;
    stream >> block.nBits;
    stream >> block.nNonce;
    block.nNonce64 = 0;
    block.mixHash.SetNull();
    stream >> TX_WITH_WITNESS(block.vtx);
    if (on_disk_version & BLOCK_VERSION_GPU_KAWPOW) {
        block.nVersion = on_disk_version;
    }
    return true;
}

} // namespace

namespace {

thread_local int g_block_header_unserialize_height = -1;
thread_local const Consensus::Params* g_block_header_unserialize_params = nullptr;

static const Consensus::Params& DefaultUnserializeParams()
{
    // Keep the CChainParams instance alive for the lifetime of the reference.
    // Binding a reference directly to CChainParams::Main()->GetConsensus() would
    // dangle: Main() returns a unique_ptr by value (a temporary) that is freed at
    // the end of the full-expression, leaving the consensus reference pointing at
    // freed memory. That use-after-free read different values per platform/allocator
    // (e.g. nDualPowActivationHeight==0 on Windows), which silently skipped the
    // KawPow nNonce64/mixHash fields when reloading the block index from disk and
    // produced a wrong block hash -> "CheckBlockProofOfWork failed".
    static const std::unique_ptr<const CChainParams> params = CChainParams::Main();
    return params->GetConsensus();
}

} // namespace

BlockHeaderUnserializeScope::BlockHeaderUnserializeScope(int nHeight, const Consensus::Params& params)
    : m_prev_height(g_block_header_unserialize_height),
      m_prev_params(g_block_header_unserialize_params)
{
    g_block_header_unserialize_height = nHeight;
    g_block_header_unserialize_params = &params;
}

BlockHeaderUnserializeScope::~BlockHeaderUnserializeScope()
{
    g_block_header_unserialize_height = m_prev_height;
    g_block_header_unserialize_params = m_prev_params;
}

int BlockHeaderUnserializeHeight()
{
    return g_block_header_unserialize_height;
}

const Consensus::Params& BlockHeaderUnserializeParams()
{
    return g_block_header_unserialize_params ? *g_block_header_unserialize_params : DefaultUnserializeParams();
}

void ReadBlockHeaderNonceFields(DataStream& stream, CBlockHeader& header, int nHeight,
                                const Consensus::Params& params)
{
    if (nHeight >= 0 && MultiAlgoRaceActive(nHeight, params)) {
        // V6 rolling-safe gate (matches CBlockHeader::SERIALIZE_METHODS read path):
        // at/after the multi-algo race the extended (nNonce64+mixHash) layout is
        // signalled ONLY by BLOCK_VERSION_V6_EXTENDED (bit 0), which lies outside the
        // BIP320 version-rolling domain. A SHA ASIC that rolls bits 17/18 therefore
        // still reads a 4-byte nNonce here and does not desync a headers stream.
        if (header.nVersion & BLOCK_VERSION_V6_EXTENDED) {
            stream >> header.nNonce64;
            stream >> header.mixHash;
            return;
        }
        stream >> header.nNonce;
        header.nNonce64 = 0;
        header.mixHash.SetNull();
        return;
    }
    if (nHeight >= 0 && !NeedsLegacyShaBlockHeaderDeserialize(nHeight, params, header.nVersion)) {
        // Below activation: bit 18 (RandomX) is IGNORED (byte-identical to v31.0.8) so
        // historical bit-18 SHA blocks (e.g. height 13190) still read 4-byte nNonce;
        // bit 17 marks a KawPow extended header unless this is a legacy SHA slot.
        if (header.nVersion & BLOCK_VERSION_GPU_KAWPOW) {
            stream >> header.nNonce64;
            stream >> header.mixHash;
            return;
        }
    }
    stream >> header.nNonce;
    header.nNonce64 = 0;
    header.mixHash.SetNull();
}

void ReadBlockHeaderFromNetwork(DataStream& stream, CBlockHeader& header, int nHeight,
                                const Consensus::Params& params)
{
    stream >> header.nVersion;
    stream >> header.hashPrevBlock;
    stream >> header.hashMerkleRoot;
    stream >> header.nTime;
    stream >> header.nBits;
    ReadBlockHeaderNonceFields(stream, header, nHeight, params);
}

bool NeedsLegacyShaBlockHeaderDeserialize(int nHeight, const Consensus::Params& params, int32_t version)
{
    // V6 race (height-gated): at/after activation, bit 17 (KawPow) / bit 18 (RandomX) are
    // genuine extended headers and SHA is non-extended, so no "legacy" special-case applies.
    if (MultiAlgoRaceActive(nHeight, params)) {
        return false;
    }
    // Below activation: byte-identical to v31.0.8 — bit 18 is IGNORED entirely. The prior
    // attempt returned false here on the RandomX bit with no height gate, which misread
    // historical bit-18 SHA blocks and crashed index load at height 13190 (proofs/02).
    if ((version & BLOCK_VERSION_GPU_KAWPOW) == 0) {
        return false;
    }
    return !IsGpuBlockHeight(nHeight, params);
}

bool DeserializeBlockFromBytes(CBlock& block, std::span<const std::byte> data, int nHeight,
                               const Consensus::Params& params)
{
    block.SetNull();
    if (data.empty()) {
        return false;
    }

    const int32_t stored_version = data.size() >= sizeof(int32_t)
        ? ReadLE32(reinterpret_cast<const unsigned char*>(data.data()))
        : 0;

    try {
        if (NeedsLegacyShaBlockHeaderDeserialize(nHeight, params, stored_version)) {
            int32_t ignored_version;
            return DeserializeLegacyShaBlockFromBytes(block, data, ignored_version);
        }
        // Bind the header (de)serialization height so the height-gated extended-header
        // detection uses this block's real height (not a stale/-1 thread-local).
        BlockHeaderUnserializeScope scope(nHeight, params);
        SpanReader{data} >> TX_WITH_WITNESS(block);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

uint256 CBlockHeader::GetHash() const
{
    // Pre-fork SHA blocks may set version bit 17 without KawPow fields; hash as SHA.
    if (IsGpuKawPowHeader() && (nNonce64 != 0 || !mixHash.IsNull())) {
        return (HashWriter{} << *this).GetHash();
    }
    return (HashWriter{} << nVersion << hashPrevBlock << hashMerkleRoot << nTime << nBits << nNonce).GetHash();
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
