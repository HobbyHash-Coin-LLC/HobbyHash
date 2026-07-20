// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#ifndef BITCOIN_RANDOMX_CHECK_H
#define BITCOIN_RANDOMX_CHECK_H

#include <cstdint>

class CBlockHeader;
namespace Consensus {
struct Params;
}

/** RandomX PoW over KawPow-style header hash (version/prev/merkle/time/bits/height) + nNonce64. */
bool CheckRandomXProofOfWork(const CBlockHeader& block, int height, const Consensus::Params& params);

/** Compute RandomX digest into out32 (32 bytes). Returns false on failure. */
bool ComputeRandomXHash(const CBlockHeader& block, int height, unsigned char* out32);

#endif // BITCOIN_RANDOMX_CHECK_H
