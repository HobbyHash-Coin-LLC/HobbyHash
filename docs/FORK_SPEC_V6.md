# HOBC V6 Multi-Algo Race — Fork Specification

**Document version:** 2026-07-20  
**Publisher:** HobbyHash Coin LLC  
**Status:** Mainnet specification (HobbyHash Core v31.1.0)  
**Base:** HobbyHash Core lineage derived from Bitcoin Core; V6 race activates at height 16800.

> **Operator note:** Official live `pool_id` assignments used by HobbyHash Coin LLC pools are
> documented in `docs/POOL_COMPLIANCE.md` (ids **1–5**). Older draft tables in this file that
> list `0=main` are historical design notes — configure pools using the compliance document.

## Activation

| Parameter | Mainnet | Testnet | Regtest |
|-----------|---------|---------|---------|
| `nMultiAlgoRaceActivationHeight` | **16800** | low (e.g. 200) | low (e.g. 200) |
| Package | HobbyHash Core **v31.1.0** (bump from 31.0.8) | same | same |
| Protocol version | **70018** | same | same |

Blocks **below** activation: **unchanged** V5 dual-PoW (3 SHA / 3 KawPow by `height % 6`) + V5.1 LWMA + V4 TH subsidy on SHA heights. Byte-identical to v31.0.8.

## HARD backward-compatibility rule (root cause of the prior 16700 failure)

The previous attempt (v31.0.9) crashed on index load at height **13190** because the new
RandomX version bit `0x00040000` (bit 18) was used in header interpretation **without a
height gate**. Bit 18 is inside the BIP320 version-rolling range `0x1fffe000`, so 1,760
historical pre-activation blocks already had it set; a SHA block that rolled bit 18 was
misread as an extended (KawPow/RandomX) header → wrong hash → `CheckBlockProofOfWork failed`.
Full analysis: `V6/proofs/02`.

**Therefore, mandatory for all V6 code:**

1. **Every** new header/PoW/algo code path is strictly gated by `nHeight >= nMultiAlgoRaceActivationHeight`.
2. For `nHeight < activation`, behavior — including `CBlockHeader` (de)serialization, extended-header detection, algo classification, and `NeedsLegacyShaBlockHeaderDeserialize` — MUST be **byte-identical to pristine v31.0.8**. The RandomX bit is **ignored** below activation.
3. **No change** to on-disk `CDiskBlockIndex` serialization layout for historical entries. New per-algo fields (`nBitsRx`) are memory-only / rebuilt on load (like `nBitsSha`/`nBitsGpu`).
4. Every V6 binary MUST pass the **index-load + reindex** test against `V6/chain-fixture` (real chain to tip) before any promotion — this is the regression guard for the 13190 class.

## Consensus model

Simultaneous **race** of three algorithms. The next tip height (≥ activation) accepts
**exactly one** valid block of any algo; first valid PoW wins.

| Algo | Header marker (post-activation only) | Difficulty track |
|------|--------------------------------------|------------------|
| SHA256d | `BLOCK_VERSION_V6_EXTENDED` (bit 0) CLEAR; no `nNonce64`/`mixHash` | `nBitsSha` |
| KawPow | bit 0 SET + `nVersion & 0x00020000` (bit 17) + `nNonce64`/`mixHash` | `nBitsGpu` |
| RandomX | bit 0 SET + `nVersion & 0x00040000` (bit 18) + `nNonce64`/`mixHash` (RX result in `mixHash`) | `nBitsRx` |

### Rolling-safe extended-header bit (bit 0) — length determinism

The header byte length depends on the algo (SHA = legacy 80-byte 4-byte-`nNonce`; KawPow/
RandomX = 116-byte `nNonce64`+`mixHash`). A SHA ASIC legally version-rolls the BIP320 domain
`0x1fffe000` (bits 13-28), which **includes the algo-signal bits 17/18** — and real firmware
(e.g. NerdOCTAXE/ESP-Miner) **ignores the restricted mask a pool advertises** and rolls the
full `0x1fffe000` anyway. If length keyed on bits 17/18, a rolled SHA header would be misread
as 116-byte and desync the wire/disk/headers stream (observed as `high-hash` / `bad-hobc-marker`).

**Fix:** the extended (116-byte) layout is signalled ONLY by `BLOCK_VERSION_V6_EXTENDED`
(`0x00000001`, bit 0), which is **outside** `0x1fffe000` and is never touched by AsicBoost
rolling. At/after activation:

- **(De)serialization length** (`CBlockHeader` SERIALIZE_METHODS read path,
  `ReadBlockHeaderNonceFields`) gates on **bit 0**, not bits 17/18.
- **Algo classification** (`AlgoFromVersionFields`) keys on **field presence**
  (`nNonce64!=0 || !mixHash.IsNull()`): a SHA header (no ext fields) classifies as SHA
  regardless of rolled bits 17/18; a genuine RandomX/KawPow header (which always carries
  `mixHash` — `CheckRandomXProofOfWork` rejects a null `mixHash`) classifies by bit 18/17.
- **PoW** (`CheckBlockProofOfWorkInner`, race branch) validates a SHA-classified header as
  SHA256d and **tolerates** rolled bits 17/18 as AsicBoost noise (no longer rejected).
- Bit 0 is stamped by `getblocktemplate` for the `kawpow`/`randomx` rules and re-asserted after
  the BIP9 version-bits loop; pools serialize `rpcData.version` verbatim (mine + submit), so
  KPSS/CPU-RandomX inherit it with no pool code change. SHA (ckpool) templates never set it.
- Bit 0 is not a BIP9 deployment bit (deployments use bit 28 TESTDUMMY, bit 2 TAPROOT).

Below activation the extended layout still keys on bit 17 (legacy V5 KawPow), unchanged.

### Fairness / dead algo

- Independent LWMA per algo, over **same-algo ancestors only**.
- Per-algo target spacing **630 s** → expected race spacing ≈ **210 s** with three live algos; ~⅓ long-run share each.
- Dead algo stops winning; chain continues (2 live ≈ 315 s, 1 live ≈ 630 s). Chain never stalls.
- A stronger algo raises only its own difficulty; it cannot freeze the other tracks.

### Subsidy (locked)

At/after activation, **all** algos pay a flat:

```text
subsidy = 45 * COIN >> ((height - 2) / 840000)
```

No TH/PH tiers, no `EstimateNetworkHashPS`, no per-algo reward. Pre-activation historical
subsidies unchanged (replay).

### Replay / fork id

| Item | Value |
|------|--------|
| SIGHASH fork id (post-V6) | `0x00016800` |
| Protocol version | `70018` |
| P2P magic | evaluate; only change if required (document if bumped) |

## Pools (post-activation)

| Port | Algo | Notes |
|------|------|-------|
| 5555 / 5556 / 5557 | SHA (main / nano / pplns) | Always-on; no GPU-window idle/gate/ALT |
| 5558 | KawPow (KPSS) | Always-on; no SHA-window idle |
| **5559** | **RandomX** | New CPU solo pool + payoutd |

Share isolation (MUST): each pool rejects wrong-algo submits (zero credit); every new tip
invalidates stale jobs; no ALT height bounce for HOBC-only; isolated sharelogs / payoutd /
stats identity per algo; unified SHA policy across main/nano/pplns.

### Coinbase marker emission (ckpool config) — AS BUILT (main ckpool)
Each pool stamps the HOBC marker into the coinbase via `generate_coinbase()` (replaces `btcsig`
to stay within the 100-byte scriptSig cap). New `ckpool.conf` keys:

| key | meaning |
|---|---|
| `hobc_marker` | `1` to emit the marker (else legacy btcsig) |
| `hobc_pool_id` | numeric pool id (matches `submitcensus` / explorer) |
| `hobc_algo` | `0` sha256, `1` kawpow, `2` randomx (this pool's algo) |
| `hobc_pool_site` | pool website/IP, <=24 bytes |
| `hobc_census_token` | shared secret for node `submitcensus` (empty = census emit off) |

The KPSS GPU pool (Node.js) uses the same wire format via `lib/hobc_marker.js`, config through env
(`HOBC_MARKER`, `HOBC_POOL_ID`, `HOBC_ALGO`, `HOBC_POOL_SITE`, `HOBC_CENSUS_TOKEN`). JS marker bytes are
byte-identical to the C builder (cross-checked, proof 10).

`winning_share_diff` in the marker = `wb->network_diff` (min difficulty a solving share must meet).
The **exact solving-share difficulty, rig model and worker name are per-worker → reported OFF-CHAIN**
to the site DB at block-solve (the shared coinbase is committed before any share solves the block).
Upgraded pools may stamp the marker at all heights (the node ignores it below activation), which
guarantees compliance at 16800 without a fragile per-pool height gate.

### Census heartbeat (pool → node) — AS BUILT
Once/min each pool calls node `submitcensus token pool_id {algo:{hashrate,unique_miners,workers}}`.
- **ckpool (SHA main/nano/pplns):** emitted from the `statsupdate` path (`hobc_maybe_send_census`),
  reusing the configured bitcoind RPC endpoint; hashrate from `dsps1`, users/workers from pool stats.
- **KPSS GPU (KawPow):** emitted from the server via a `setInterval` `submitcensus` POST; hashrate from
  accumulated accepted-share difficulty, workers/miners from authorized stratum clients.
- **CPU (RandomX):** baked in when that pool is built (`algo=2`).
Worker IPs and exact per-worker data are reported off-chain to the site DB (not in the census RPC).
All census bodies verified wire-accepted and aggregated by the node (proofs 09/10).

## Version-rolling mask (pool → miner) — best-effort only

Pools MAY advertise (via stratum `mining.configure`) a version-rolling mask that clears algo bits
17 (0x20000) and 18 (0x40000). **This is best-effort and the node does NOT rely on it.** Real ASIC
firmware (e.g. NerdOCTAXE/ESP-Miner) **ignores the granted mask** and rolls the full `0x1fffe000`,
setting bits 17/18 on plain SHA blocks. Correctness is therefore enforced node-side by the
rolling-safe `BLOCK_VERSION_V6_EXTENDED` bit 0 + field-presence classification (see "Consensus
model" above): a rolled SHA block decodes as a legacy 80-byte header and validates as SHA256d,
tolerating rolled bits 17/18. KawPow/RandomX miners do not version-roll.

## Node block assembler
At/after activation, the node assembler (`node/miner.cpp`) stamps a SHA-algo HOBC marker
(`pool_id=0, algo=0`) into its own coinbase so `getblocktemplate` self-check and `generatetoaddress`
stay valid; pools replace the coinbase with their own marker. Below activation no marker is added.

## RPC / GBT

`getblocktemplate` / `getmininginfo` expose `bits_sha`, `bits_gpu`, `bits_rx`, `powalgos`,
and `race_active` for race mode. Templates issuable for all three algos every height ≥ activation.

## Algo discriminator (FINAL — robust to third-party pools/miners)

At heights **≥ activation**, a block's algo is bound by **two** independent signals that must agree:

1. **Extended-header field presence + bit 0.** A genuine KawPow/RandomX header sets
   `BLOCK_VERSION_V6_EXTENDED` (bit 0, rolling-safe) and carries `nNonce64`/`mixHash`; bit 18 → RandomX,
   bit 17 (not 18) → KawPow. A SHA header carries no ext fields (and bit 0 is clear) → SHA256d,
   **regardless** of any AsicBoost-rolled bits 17/18. Classification (`AlgoFromVersionFields`) keys on
   field presence, so rolled algo bits on a SHA block are harmless noise — the node does not need to
   control third-party SHA miners or their firmware's version-rolling behavior.
2. **Coinbase telemetry marker** `algo` field (below). The node **rejects** the block if
   `marker.algo != header-derived algo`. ckpool stamps `algo=0` (SHA), so a rolled SHA block that
   classifies as SHA still dual-binds correctly.

RandomX PoW additionally requires a non-null `mixHash` matching the recomputed RandomX digest, and
KawPow requires a valid progpow verify — so a header cannot claim an algo without the real PoW.
**At heights < activation, bits 17/18 and the coinbase marker are ignored
entirely** (byte-identical to v31.0.8; fixes the 13190 class; keeps replay).

## Coinbase telemetry marker + compliant-pool enforcement (FINAL)

**Enforcement model:** *format-required at consensus* (NOT private-key gatekeeping). From activation
the node rejects any block whose coinbase lacks a well-formed HOBC marker. This forces every pool
onto our reporting software while keeping mining open to any *compliant* build.

### Byte budget (measured, not guessed)

- Coinbase `scriptSig` consensus limit: **2..100 bytes** (`consensus/tx_check.cpp:49`).
- Real mainnet coinbase today (block 16670): **52 bytes** = height(4) + OP_0(1) + extranonce(~12) +
  legacy `ckpool`(7) + `/HOBC-main-solo/`(16).
- Replacing the legacy `ckpool`+btcsig tail (~23 B) frees the space; **marker payload budget ≈ 70 B**
  (100 − height/OP_0/extranonce ≈ 29 − 1 push opcode). Pools MUST size `nonce1length+nonce2length`
  so height + extranonce + marker ≤ 100; the node already enforces the 100-byte cap.

### Marker layout (`HOBC` TLV, marker version 1)

```
magic   : "HOBC"            (4 bytes, 0x48 0x4F 0x42 0x43)
version : 0x01              (1 byte)
then TLV entries [type(1) len(1) value(len)] until end of marker:
  0x01 pool-id            len 1   REQUIRED  (0=main 1=nano 2=pplns 3=gpu 4=cpu; 5..255 registered)
  0x02 algo               len 1   REQUIRED  (0=sha256 1=kawpow 2=randomx)
  0x03 winning-share-diff len 4   REQUIRED  (IEEE-754 float32 LE; telemetry precision)
  0x04 pool-site          len ≤24 optional  (ascii host/label, truncated to fit)
  0x05 rig-model          len ≤16 optional  (from stratum useragent, e.g. "Antminer S19")
  0x06 worker-name        len ≤16 optional  (truncated; NEVER the miner IP)
```

Required minimum = magic4+ver1 + poolid(3) + algo(3) + diff(6) = **17 B**. Optional text shares the
remaining ~53 B; pools truncate optional fields first if space-constrained. **Consensus check
(≥ activation, in `ContextualCheckBlock`):** magic+version present; the three REQUIRED TLVs present
and well-formed; `algo` ∈ {0,1,2} and equals header algo; unknown TLV types ignored (forward-compat).
Missing/malformed/mismatch → block **invalid**. Below activation the coinbase is **not** inspected.

### Data homes

- **On-chain (marker):** pool-id, algo, winning-share-diff, pool-site, rig-model, worker-name.
- **OFF-chain (never on the public chain):** miner/worker **IP** and any other private telemetry —
  sent by our pools to the site over an authenticated report, stored in the site DB for display only.

## Node network census (baked into the node) — AS BUILT (v31.1.0)

Memory-only subsystem `node/network_census.{h,cpp}`; nothing persisted (no disk-format change).

### `submitcensus token pool_id report`  (authenticated pool → node heartbeat)
- Auth: `token` constant-time compared to node `-censustoken`. Unset token ⇒ RPC disabled.
  Wrong token ⇒ `-32600 "census token rejected"`.
- `pool_id` (int, matches coinbase marker pool-id).
- `report` (object):
  ```json
  {"pool_name":"HobbyHash Main","pool_site":"hobbyhashcoin.com",
   "sha256":{"hashrate":1.2e15,"unique_miners":40,"workers":120},
   "kawpow":{"hashrate":5.0e9,"unique_miners":12,"workers":30},
   "randomx":{"hashrate":2.5e5,"unique_miners":8,"workers":15}}
  ```
  Any algo object may be omitted. Worker **IPs are NOT sent here** — off-chain to the site DB only.
- Node stores the pool's latest report; pools push once per minute.

### `getnetworkstats [nblocks=120] [max_samples=60]`  (live feed; poll without page refresh)
Returns:
- `reported` — summed across pools fresh within 150s: per-algo `{hashrate,unique_miners,workers}` +
  `total` + `active_pools`.
- `node_estimate` — node's **independent** per-algo hashrate from the chain (sum of per-block
  `GetBlockProof` per algo over `nblocks`, ÷ window wall-clock span; algo via height-gated `AlgoAtIndex`).
- `pools[]` — active pool detail (id, name, site, last_seen, per-algo).
- `series[]` — rolling per-minute samples (24h max = 1440), reported totals + node estimate per sample.
- `race_active` — whether the tip height ≥ activation.

### `getblock` telemetry fields (verbosity ≥ 1, and REST)
- `powalgo` — always (`sha256`/`kawpow`/`randomx`, via height-gated `AlgoFromHeader`).
- `hobc_marker` — present only when a well-formed marker is found (≥ activation): `version`, `pool_id`,
  `algo`, `algo_name`, `winning_share_diff`, and optional `pool_site`, `rig_model`, `worker_name`.
  Decoded directly from the coinbase (no `CBlockIndex`/disk-format change).

## Test-driven delivery (every change proven)

Ordered, each with saved proof under `V6/proofs/`:

1. `01` baseline load (DONE) · `02` root cause (DONE)
2. Consensus unit tests: accept/reject per algo, independent LWMA, dead-algo progress, fixed subsidy=45, and **index-load/reindex of the real chain fixture**.
3. RPC/GBT tests.
4. Pool submit/rejection + tip-kill tests (share matrix).
5. CPU RandomX pool end-to-end (verify + coinbase + payout dry-run).
6. Regtest activation rehearsal at low height (GBT all 3, wrong-algo reject, coinbase=45, restart/reindex across boundary).
7. Testnet burn-in: timing CSV (210/315/630), share matrix under load, orphan rate.
8. Feature regression (sends across boundary, payouts SHA/GPU/CPU, explorer/stats, wallet GPU vs always-on KPSS).
9. Go/no-go + explicit user sign-off. Slip height if not green with lead time.

No promotion to any live node/pool/wallet until 9 is signed off.
