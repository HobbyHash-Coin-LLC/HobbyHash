// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include <primitives/transaction.h>
#include <consensus/dual_pow.h>
#include <serialize.h>
#include <uint256.h>
#include <util/time.h>

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Consensus {
struct Params;
}

class DataStream;
class CBlockHeader;

bool NeedsLegacyShaBlockHeaderDeserialize(int nHeight, const Consensus::Params& params, int32_t version);
int BlockHeaderUnserializeHeight();
const Consensus::Params& BlockHeaderUnserializeParams();
void ReadBlockHeaderNonceFields(DataStream& stream, CBlockHeader& header, int nHeight,
                                const Consensus::Params& params);
void ReadBlockHeaderFromNetwork(DataStream& stream, CBlockHeader& header, int nHeight,
                                const Consensus::Params& params);

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    static constexpr int32_t GPU_KAWPOW_VERSION = BLOCK_VERSION_GPU_KAWPOW;

    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    /** KawPow fields (present when nVersion has GPU_KAWPOW_VERSION). */
    uint64_t nNonce64{0};
    uint256 mixHash;

    CBlockHeader()
    {
        SetNull();
    }

    bool IsGpuKawPowHeader() const { return (nVersion & GPU_KAWPOW_VERSION) != 0; }
    bool IsCpuRandomXHeader() const { return (nVersion & BLOCK_VERSION_CPU_RANDOMX) != 0; }

    SERIALIZE_METHODS(CBlockHeader, obj)
    {
        READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits);
        if (ser_action.ForRead()) {
            const int h = BlockHeaderUnserializeHeight();
            const Consensus::Params& cp = BlockHeaderUnserializeParams();
            bool extended;
            if (h >= 0 && MultiAlgoRaceActive(h, cp)) {
                // V6 rolling-safe gate: at/after the multi-algo race the extended
                // (nNonce64+mixHash) layout is signalled ONLY by BLOCK_VERSION_V6_EXTENDED
                // (bit 0), which lies OUTSIDE the BIP320 version-rolling domain 0x1fffe000.
                // A SHA ASIC that AsicBoost-rolls bits 17/18 therefore still decodes as a
                // legacy 80-byte header here (bit 0 stays clear), instead of misreading 40
                // bytes and desyncing the wire/disk stream.
                extended = (obj.nVersion & BLOCK_VERSION_V6_EXTENDED) != 0;
            } else if (h < 0 && (obj.nVersion & BLOCK_VERSION_CPU_RANDOMX) &&
                                (obj.nVersion & BLOCK_VERSION_V6_EXTENDED)) {
                // Unknown height (h<0, e.g. blk-file reindex scan before the block is linked
                // to a parent). A race RandomX header sets bit 18 (CPU_RANDOMX) WITHOUT bit 17,
                // so the legacy bit-17 branch below would read it as a 4-byte nNonce and desync.
                // Detect it by bit 18 AND the rolling-safe BLOCK_VERSION_V6_EXTENDED bit (bit 0),
                // which only genuine V6 GBT sets. This must NOT be bit 0 alone: bit 0 collides
                // with legacy/genesis version=1 headers (and any odd base version), which have
                // no extended fields and must decode as legacy 80-byte headers. Requiring bit 18
                // too excludes genesis (bit 18 clear) and real SHA blocks (even base versions).
                extended = true;
            } else {
                // Below activation (or unknown height h<0): byte-identical to v31.0.8 —
                // bit 18 (RandomX) is IGNORED; bit 17 marks a KawPow extended header unless
                // this is a legacy SHA slot. Historical bit-18 SHA blocks (e.g. 13190) read
                // a 4-byte nNonce (this is the path that crashed index load in proofs/02).
                extended = (obj.nVersion & GPU_KAWPOW_VERSION) &&
                           (h < 0 || !NeedsLegacyShaBlockHeaderDeserialize(h, cp, obj.nVersion));
            }
            if (extended) {
                READWRITE(obj.nNonce64, obj.mixHash);
            } else {
                READWRITE(obj.nNonce);
            }
        } else {
            // Write: a header is extended iff it actually carries the 64-bit nonce / mixHash AND
            // sets an algo bit. Pre-activation SHA blocks never populate these fields, so this is
            // byte-identical to v31.0.8 for every historical block, and correct for KawPow/RandomX.
            const bool has_ext_fields = (obj.nNonce64 != 0 || !obj.mixHash.IsNull());
            const bool extended = has_ext_fields &&
                ((obj.nVersion & GPU_KAWPOW_VERSION) || (obj.nVersion & BLOCK_VERSION_CPU_RANDOMX));
            if (extended) {
                READWRITE(obj.nNonce64, obj.mixHash);
            } else {
                READWRITE(obj.nNonce);
            }
        }
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
        nNonce64 = 0;
        mixHash.SetNull();
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    NodeSeconds Time() const
    {
        return NodeSeconds{std::chrono::seconds{nTime}};
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};


class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // Memory-only flags for caching expensive checks
    mutable bool fChecked;                            // CheckBlock()
    mutable bool m_checked_witness_commitment{false}; // CheckWitnessCommitment()
    mutable bool m_checked_merkle_root{false};        // CheckMerkleRoot()

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *(static_cast<CBlockHeader*>(this)) = header;
    }

    SERIALIZE_METHODS(CBlock, obj)
    {
        READWRITE(AsBase<CBlockHeader>(obj), obj.vtx);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
        m_checked_witness_commitment = false;
        m_checked_merkle_root = false;
    }

    std::string ToString() const;
};

/** True when version bit 17 is set on a SHA slot (legacy 80-byte header on disk/wire). */

/** Height-aware block decode for blk storage and RPC (handles spurious GPU version bit pre-fork). */
bool DeserializeBlockFromBytes(CBlock& block, std::span<const std::byte> data, int nHeight,
                               const Consensus::Params& params);

/** RAII: thread-local header height for CBlockHeader serialization during P2P decode. */
class BlockHeaderUnserializeScope
{
    int m_prev_height;
    const Consensus::Params* m_prev_params;

public:
    BlockHeaderUnserializeScope(int nHeight, const Consensus::Params& params);
    ~BlockHeaderUnserializeScope();

    BlockHeaderUnserializeScope(const BlockHeaderUnserializeScope&) = delete;
    BlockHeaderUnserializeScope& operator=(const BlockHeaderUnserializeScope&) = delete;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    /** Historically CBlockLocator's version field has been written to network
     * streams as the negotiated protocol version and to disk streams as the
     * client version, but the value has never been used.
     *
     * Hard-code to the highest protocol version ever written to a network stream.
     * SerParams can be used if the field requires any meaning in the future,
     **/
    static constexpr int DUMMY_VERSION = 70016;

    std::vector<uint256> vHave;

    CBlockLocator() = default;

    explicit CBlockLocator(std::vector<uint256>&& have) : vHave(std::move(have)) {}

    SERIALIZE_METHODS(CBlockLocator, obj)
    {
        int nVersion = DUMMY_VERSION;
        READWRITE(nVersion);
        READWRITE(obj.vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
