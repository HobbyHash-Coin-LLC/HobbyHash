// Copyright (c) 2026 The HobbyHash Core developers
// Distributed under the MIT software license.

#ifndef BITCOIN_KAWPOW_CHECK_H
#define BITCOIN_KAWPOW_CHECK_H

#include <uint256.h>

class CBlockHeader;

namespace Consensus {
struct Params;
}

uint256 KawPowHeaderHash(const CBlockHeader& block, int height);
bool CheckKawPowProofOfWork(const CBlockHeader& block, int height, const Consensus::Params& params);

#endif // BITCOIN_KAWPOW_CHECK_H
