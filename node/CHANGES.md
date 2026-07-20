# HobbyHash Core V6 — node source changes

**Publisher:** HobbyHash Coin LLC  
**Base:** Bitcoin Core–derived HobbyHash tree, package **v31.1.0**  
**Purpose of this document:** explain **what we changed** in the node and **why**, so operators and auditors can review the fork safely.

Paths below are relative to `node/AL10/` or `node/AL9/` (identical trees).

---

## Consensus / multi-algo race

| File | Change | Why |
|------|--------|-----|
| `src/kernel/chainparams.cpp` | Sets `nMultiAlgoRaceActivationHeight = 16800` (mainnet) and related V6 params | Turns on the three-algo race only after a fixed height so history stays replay-safe |
| `src/consensus/params.h` | Adds race / RandomX / marker-related consensus fields | Gives the rest of the node typed parameters instead of magic numbers |
| `src/consensus/dual_pow.h` / `dual_pow.cpp` | Algo classification, extended-header bit 0, KawPow/RandomX version bits | Prevents AsicBoost version-rolling from changing header length; classifies SHA vs KawPow vs RandomX correctly |
| `src/pow.h` / `pow.cpp` | Per-algo difficulty tracks and race PoW checks | Each algo has its own LWMA; first valid PoW wins the height |
| `src/pow.cpp` / `src/consensus/params.h` / `src/kernel/chainparams.cpp` | From height **16950**, race LWMA solvetime uses the prior **same-algo** block only (not tip gap), with per-algo target **390 s** (~2.16 min race spacing with three live algos) | Stops other algos from making a slow SHA/GPU/CPU win look “fast”; each algo tracks its own hits |
| `src/primitives/block.h` / `block.cpp` | Header fields `nNonce64`, `mixHash`; serialize length gated on bit 0 | KawPow/RandomX need extended headers; SHA stays 80-byte |
| `src/chain.h` | Per-algo bits helpers / index fields used in memory | Supports independent difficulty tracks without rewriting ancient disk index layout incorrectly |
| `src/validation.cpp` | Height-gated PoW and marker checks at connect | Rejects invalid race blocks; ignores new rules below activation |

## HOBC coinbase marker (required for pools)

| File | Change | Why |
|------|--------|-----|
| `src/consensus/hobc_marker.h` / `hobc_marker.cpp` | Encode/decode HOBC TLV marker | Standard wire format shared with all HobbyHash pools |
| `src/node/miner.cpp` | Node assembler can stamp a SHA marker on templates | Keeps local mining / GBT compliant after activation |
| `src/rpc/blockchain.cpp` | Surfaces `powalgo` and `hobc_marker` on `getblock` | Explorers and wallets can show algo + pool attribution |
| Consensus tx checks (100-byte scriptSig) | Marker must fit coinbase scriptSig budget | Bitcoin-family rule; pools replace bulky btcsig text with compact HOBC TLVs |

**Policy:** after activation, blocks **without** a valid HOBC marker are **not accepted**. Marker identity fields must be real; see `docs/POOL_COMPLIANCE.md`.

## Network census (compliant pools)

| File | Change | Why |
|------|--------|-----|
| `src/node/network_census.h` / `network_census.cpp` | In-memory per-`pool_id` heartbeats | Aggregates hashrate / miners / workers for the public race page |
| `src/rpc/telemetry.cpp` | RPCs `submitcensus` and `getnetworkstats` | Pools push once per minute; sites poll live stats |
| `src/init.cpp` | `-censustoken` argument | Shared secret so only authorized reporters can submit census |

Census bodies intentionally **omit miner IPs** (privacy / abuse resistance).

## Mining RPC / GBT

| File | Change | Why |
|------|--------|-----|
| `src/rpc/mining.cpp` | Multi-algo `getblocktemplate` rules, version bits for KawPow/RandomX | GPU/CPU pools receive correct templates; SHA templates stay legacy-shaped |

## Subsidy

| File | Change | Why |
|------|--------|-----|
| `src/consensus/hashrate_subsidy.cpp` (and call sites) | After activation, flat `45 * COIN` halved on the existing schedule for **all** algos | Removes TH/PH tier games from the race; fair equal reward |

## RPC display hardening

| File | Change | Why |
|------|--------|-----|
| `src/rpc/util.cpp` (`GetTarget`) | Uses RandomX pow limit for RandomX tips; non-fatal fallback if compact bits are odd | Stops `getblockchaininfo` from aborting on RandomX tips (wallet / status UIs) |

## Tests

| File | Change | Why |
|------|--------|-----|
| `src/test/v6_multi_algo_tests.cpp` | Unit coverage for race / marker / header rules | Guards regressions (especially pre-activation header bit handling) |

## Packaging (HobbyHash Coin LLC)

| File | Change | Why |
|------|--------|-----|
| `scripts/build-linux-al10-release.sh` | Builds `HobbyHash-Linux-Node-x86_64.tar.gz` | Standard / AlmaLinux 10 class hosts |
| `scripts/build-linux-al9-container.sh` | Builds `HobbyHash-Linux-Node-AL9-x86_64.tar.gz` via AlmaLinux 9 container | RHEL 9 / Alma 9 / Rocky 9 class hosts |
| `packaging/al9/Containerfile` | AlmaLinux 9 builder image | Reproducible EL9 ABI |

## What we deliberately did **not** change

- Genesis, ports (P2P **18761**, RPC **18762**), ticker, and historical blocks below activation remain compatible with prior HobbyHash releases.
- Pre-activation dual-PoW schedule (SHA/KawPow windows) stays intact for replay.

---

HobbyHash Coin LLC maintains this fork for the HobbyHash Coin network. Review `docs/FORK_SPEC_V6.md` for the full specification.
