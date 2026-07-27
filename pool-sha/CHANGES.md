# HobbyHash SHA pools (ckpool) — source changes

**Publisher:** HobbyHash Coin LLC  
**Document updated:** 2026-07-27 03:55 UTC  
**Trees:** `pool-sha/hobc-main/`, `pool-sha/hobc-nano/`, `pool-sha/hobc-pplns/`

This document records HobbyHash Coin LLC patches on top of upstream ckpool and why they exist.

---

## 2026-07-27 03:55 UTC — Grant version-rolling bits 17/18 (LuxOS accept fix)

| File | Change | Why |
|------|--------|-----|
| `hobc-main/src/stratifier.c` (and nano/pplns) | In the `mining.configure` handler, narrow the withheld version-rolling bits from `~0x10060000` to `~0x10000000`. Bit 28 stays withheld; bits 17 (`0x20000`) and 18 (`0x40000`) are now granted, so the advertised mask is `0fffe000` instead of `0ff9e000`. | LuxOS firmware rolls the full BIP320 domain regardless of the granted mask, but reports `version_bits` masked down to the grant. With bits 17/18 withheld the pool rebuilt a header without them and rejected roughly 85% of that miner's shares as `Above target`. Granting them is safe: consensus classifies a header's algorithm by extended-field presence (`nNonce64`/`mixHash`), not by bits 17/18 — see `AlgoFromVersionFields()` — and plain SHA blocks carrying rolled bits 17/18 are already accepted on mainnet. |

**Verified:** LuxOS S19j Pro (`LUXminer 2026.7.14`) went from ~14% to 100% accepted on `:5555` with no reconnect storm; all other miners stayed at 100%.

**Scope:** version-rolling mask negotiation only. Share validation already masked with the full `0x1fffe000` domain and is unchanged, as are payout math, marker encoding, and census.

---

## 2026-07-27 03:08 UTC — Withhold pre-auth jobs when coinb2 is per-client

| File | Change | Why |
|------|--------|-----|
| `hobc-main/src/stratifier.c` | `init_client()` sends difficulty but no `mining.notify` when `hobc_marker` / `direct_worker_payout` is enabled and the client has not authorised yet. `sauth_process()` calls `update_client()` after a successful authorise. | With a per-client coinb2 the worker name is stamped into the coinbase, but before `mining.authorize` that field is blank. Firmwares that hash the pre-auth job submitted work the pool then validated against the named-worker coinbase, producing a different merkle root and mass `Above target`. Holding the first job until authorise makes the miner and pool agree on one coinbase. |

**Scope:** job delivery timing only. Miners that authorise normally receive work immediately after `mining.authorize`.

---

## 2026-07-26 19:25 UTC — Automatic HobbyHash census hub heartbeats

| File | Change | Why |
|------|--------|-----|
| `hobc-main/src/ckpool.h`, `ckpool.c` (and nano/pplns) | Add `hobc_census_hub` (default **true**) and optional `hobc_census_url` override. `hobc_census_token` remains optional for **local-node** `submitcensus` only. | Outside pools must appear in HobbyHash live census without operators configuring a census token or RPC firewall opening. |
| `hobc-main/src/stratifier.c` (and nano/pplns) | By default POST `submitcensus` once per minute to `https://hobbyhashcoin.com/api/network/census/submit/` via the system `curl` CLI. Hub ignores client tokens; HobbyHash injects with the server token. Optional local-node path kept when `hobc_census_token` is set. | Mining RPC stays on local `btcd` / `btcdurl`; census is a separate HTTPS heartbeat. No node rebuild and no public `18762`. |

**Operator note:** rebuild `ckpool` and restart. Ensure `curl` is installed. Set `hobc_pool_site` / `hobc_pool_id` correctly. Set `"hobc_census_hub": false` only if you must disable public reporting.

**Scope:** census emit path only. Marker encoding, share validation, and payout behavior unchanged.

---

## 2026-07-21 01:40 UTC — LuxOS / AsicBoost version-bit dual hash

| File | Change | Why |
|------|--------|-----|
| `hobc-main/src/stratifier.c` | In `share_diff()`, when the miner supplies version-rolling bits, hash the header twice: once with the normal rolled version (`base \| (bits & BIP320 mask)`), and once with BIP34 bit `0x4` cleared on the base (`(base & ~0x4) \| (bits & BIP320 mask)`). Keep the higher share difficulty (and matching header hash/swap). | Some ASIC firmwares (notably LuxOS) drop BIP34 bit `0x4` while AsicBoost-rolling, so they hash `0x20xxxxxx` instead of `0x20000004\|rolled`. The pool previously only checked the bit-`0x4`-preserved form, so valid LuxOS work was rejected as above target. Dual-hashing accepts both Bitaxe-style (keeps `0x4`) and LuxOS-style (drops `0x4`) shares without weakening other miners. |
| `hobc-pplns/src/stratifier.c` | Same change as main. | PPLNS uses the same SHA share validation path; miners on port 5557 need the same accept behavior as main solo (5555). |

**Scope:** share validation / accept only. Does not change difficulty adjustment, payout math, marker encoding, or census.

**Operator note:** rebuild `ckpool` from the updated `src/stratifier.c` and restart the pool process after deploy.

---

## Earlier HobbyHash patches (summary)

| Area | Files (typical) | Why |
|------|-----------------|-----|
| HOBC coinbase marker | `src/hobc_marker.c`, `src/stratifier.c` | After mainnet height 16800, SHA blocks need a valid HOBC marker |
| Config keys | `src/ckpool.c`, `src/ckpool.h` | `hobc_marker`, `hobc_pool_id`, `hobc_algo`, `hobc_pool_site`, census token |
| Network census | `src/stratifier.c` | Authorized pools report hashrate / workers via `submitcensus` |
| Version-roll bit 28 preserve | `src/stratifier.c` (`share_diff`) | OR rolled bits onto base version so ASIC overlays do not drop HOBC base bit 28 |

See also: [`README.md`](README.md), [`../docs/POOL_COMPLIANCE.md`](../docs/POOL_COMPLIANCE.md).
