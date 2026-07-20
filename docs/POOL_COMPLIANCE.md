# Pool compliance — HOBC coinbase markers (HobbyHash Coin LLC)

**Audience:** anyone running a HobbyHash mining pool after V6 activation (mainnet height **16800**).

**Interactive helper (use this before you deploy):**  
https://hobbyhashcoin.com/docs/pool-config/

That page validates `pool_site` / `pool_id` / `algo` / worker length live and generates a ready-to-copy SHA (`ckpool.conf`) or GPU/CPU (env) config.

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
4. Optional but strongly recommended: `pool_site`, `worker_name` (rig is optional; current HobbyHash builders omit it for scriptSig headroom).

Miner **IP addresses must never** be placed in the marker.

## Field limits (do not exceed these)

| Field | Limit | Notes |
|-------|------:|-------|
| `pool_id` | 1 byte (`0`–`255`) | Required. Stable per site. |
| `algo` | 1 byte | Required. `0` SHA / `1` KawPow / `2` RandomX. |
| `winning_share_diff` | 4 bytes float32 | Required (pool fills on solve). |
| `pool_site` | **≤ 24** ASCII printable chars | Optional but strongly recommended. Longer values keep the **last 24**. |
| `worker_name` | **≤ 16** ASCII printable chars | Suffix after the last `.` in the miner username. Longer values keep the **last 16**. |
| `rig_model` | ≤ 16 (consensus) | Usually omitted by current pool builders. |

ASCII printable only: characters `0x20`–`0x7e` (letters, digits, `.` `-` `_`, etc.). No emoji, no unicode hostnames in the marker.

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

## Do

- Use your **real** site hostname/label (≤ 24 chars), e.g. `pool.example.com`.
- Pick a **stable** `pool_id` for your site and keep it forever.
- Set `algo` to match the pool you run (`0` / `1` / `2`).
- Keep worker suffixes short (≤ 16), e.g. `rig1`, `1080TI`, `axe01`.
- Validate on https://hobbyhashcoin.com/docs/pool-config/ before miners connect.
- Use printable ASCII only in marker text fields.

## Don't

- Don’t set `pool_site` to `pool.hobbyhashcoin.com` unless you are HobbyHash Coin LLC.
- Don’t forge another operator’s site name or identity.
- Don’t put miner **IP addresses** in the marker or coinbase tags.
- Don’t rely on long hostnames — `solo.super-long-hostname.mining.example.com` becomes the **last 24 chars only**.
- Don’t use long worker names — `garage-basement-rig-01` becomes `basement-rig-01` (last 16).
- Don’t leave identity blank if you want explorer / network-race attribution.

## Config examples

### Good

```text
pool_site = pool.example.com          (16 chars — OK)
pool_id   = 10                        (your stable id)
algo      = 0                         (SHA pool)
worker    = hobc1q....ullu.rig1       (stamped worker suffix: rig1)
```

SHA `ckpool.conf` snippet:

```json
{
  "hobc_marker": 1,
  "hobc_pool_id": 10,
  "hobc_algo": 0,
  "hobc_pool_site": "pool.example.com"
}
```

GPU/CPU env snippet:

```bash
export HOBC_MARKER=1
export HOBC_POOL_ID=10
export HOBC_ALGO=1
export HOBC_POOL_SITE=pool.example.com
```

### Bad (will truncate or get you blocked)

```text
pool_site = solo.super-long-hostname.mining.example.com
            → stamped as: ong-hostname.mining.example.com  (last 24)

worker    = hobc1q....ullu.garage-basement-rig-01
            → stamped as: basement-rig-01                  (last 16)

pool_site = pool.hobbyhashcoin.com   → impersonation — do not do this
pool_site = my pool 🚀               → non-ASCII / spaces — sanitize/reject risk
```

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
3. Run your values through https://hobbyhashcoin.com/docs/pool-config/ — zero blocking errors.
4. Pool config has `hobc_marker=1` (or `HOBC_MARKER=1` for JS pools).
5. `hobc_pool_id` / `HOBC_POOL_ID` is set and unique for your site.
6. `hobc_algo` / `HOBC_ALGO` matches the pool (0/1/2).
7. `hobc_pool_site` / `HOBC_POOL_SITE` is your real site string (≤ 24 characters in the marker).
8. Optional census token is configured only if you have a valid token from the node operator.
9. Mine a test block on regtest/testnet or watch the first mainnet solve: explorer must show your pool label and algorithm.
10. Never put IPs in coinbase tags.

## Where each official source sets the marker

| Pool tree | Marker implementation |
|-----------|------------------------|
| `pool-sha/` | `src/hobc_marker.c` + `stratifier.c` |
| `pool-gpu/` | `lib/hobc_marker.js` |
| `pool-cpu/` | `lib/hobc_marker.js` |

## Off-chain data (IPs)

Worker IP addresses stay **off-chain** (pool logs / operator admin tools). They are not part of the public coinbase marker and are not sent in the public census RPC body.
