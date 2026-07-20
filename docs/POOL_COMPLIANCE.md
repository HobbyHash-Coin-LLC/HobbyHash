# Pool compliance — HOBC coinbase markers (HobbyHash Coin LLC)

**Audience:** anyone running a HobbyHash mining pool after V6 activation (mainnet height **16800**).

## Why this exists

HobbyHash V6 runs a fair race between three proof-of-work algorithms. Every winning block must prove which pool and algorithm produced it by stamping a small **HOBC marker** in the coinbase.

Without a valid marker, **the node will not accept the block**.

## What the node requires

At/after height 16800, a block is invalid unless:

1. The coinbase contains a well-formed **HOBC** marker (magic `HOBC`, version `1`).
2. Required TLV fields are present:
   - `pool_id` (1 byte)
   - `algo` (`0` = SHA-256d, `1` = KawPow, `2` = RandomX)
   - `winning_share_diff` (float32)
3. Marker `algo` matches the block’s header algorithm.
4. Optional but strongly recommended: `pool_site`, `rig_model`, `worker_name`.

Miner **IP addresses must never** be placed in the marker.

## Real information policy (read this)

HobbyHash Coin LLC expects marker and census fields to describe a **real pool**:

| Field | Expectation |
|-------|-------------|
| `pool_site` | Your real public domain or site label (example: `pool.example.com`) |
| `pool_id` | Stable ID you operate under (do not impersonate another operator’s site+id pair) |
| `pool_name` (census) | Honest name for your pool |
| `worker_name` / `rig_model` | Real worker / miner software identity when present |

**If you forge another operator’s site name, fake census hashrate, or stamp misleading identity tags, HobbyHash Coin LLC administrators may block your pool** (ban stratum peers, drop census trust, revoke public display names, and refuse support).

Use real data. It is how the explorer, network race page, and operators attribute blocks fairly.

## HobbyHash Coin LLC reference pool IDs

These IDs are used by official HobbyHash pools under site `pool.hobbyhashcoin.com`:

| pool_id | Pool | Algo |
|--------:|------|------|
| 1 | Main Solo (SHA) | SHA-256d |
| 2 | Nano Solo (SHA) | SHA-256d |
| 3 | PPLNS (SHA) | SHA-256d |
| 4 | CPU Solo (RandomX) | RandomX |
| 5 | GPU Solo (KawPow) | KawPow |

Third-party pools should pick their own unused `pool_id` **and** their own `pool_site`. Display names on hobbyhashcoin.com are assigned by HobbyHash Coin LLC admin tools keyed by `(pool_site, pool_id)`.

## Beginner checklist before going live

1. Node binary is **HobbyHash Core v31.1.0** or newer V6.
2. Node tip is at or past height **16800** (race active).
3. Pool config has `hobc_marker=1` (or `HOBC_MARKER=1` for JS pools).
4. `hobc_pool_id` / `HOBC_POOL_ID` is set and unique for your site.
5. `hobc_algo` / `HOBC_ALGO` matches the pool (0/1/2).
6. `hobc_pool_site` / `HOBC_POOL_SITE` is your real site string (≤ 24 characters in the marker).
7. Optional census token is configured only if you have a valid token from the node operator.
8. Mine a test block on regtest/testnet or watch the first mainnet solve: explorer must show your pool label and algorithm.
9. Never put IPs in coinbase tags.

## Where each official source sets the marker

| Pool tree | Marker implementation |
|-----------|------------------------|
| `pool-sha/` | `src/hobc_marker.c` + `stratifier.c` |
| `pool-gpu/` | `lib/hobc_marker.js` |
| `pool-cpu/` | `lib/hobc_marker.js` |

## Off-chain data (IPs)

Worker IP addresses stay **off-chain** (pool logs / operator admin tools). They are not part of the public coinbase marker and are not sent in the public census RPC body.
