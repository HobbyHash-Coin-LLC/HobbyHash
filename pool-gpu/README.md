# HobbyHash KawPow GPU pool (KPSS) — HobbyHash Coin LLC

Node.js stratum server for **KawPow** GPU mining on HobbyHash V6.

| Item | Value |
|------|-------|
| Official stratum port | **5558** |
| Official `pool_id` | **5** |
| Algo id in marker | **1** (KawPow) |
| Entry script | `hobc-mainnet-stratum-server.js` |
| Marker library | `lib/hobc_marker.js` |

Upstream KPSS notes (if present): [`README-UPSTREAM.md`](README-UPSTREAM.md).

---

## Mandatory compliance

HobbyHash Core **rejects** KawPow blocks without a valid **HOBC coinbase marker** after height **16800**.

You must set real values for:

- `HOBC_MARKER=1`
- `HOBC_POOL_ID` (official GPU pool uses `5`; third parties: pick your own)
- `HOBC_ALGO=1`
- `HOBC_POOL_SITE=your.real.domain`

**Spoofed site names or fake census data can get your pool blocked by HobbyHash Coin LLC administrators.**  
See [`../docs/POOL_COMPLIANCE.md`](../docs/POOL_COMPLIANCE.md).  
**Validate + generate env config first:** https://hobbyhashcoin.com/docs/pool-config/ (`HOBC_POOL_SITE` ≤ 24 chars).

---

## Step-by-step (beginner)

### 1) Requirements

- A synced HobbyHash Core **v31.1.0** node with RPC on `127.0.0.1:18762`
- Node.js **18+** (20 LTS recommended)
- NVIDIA or AMD GPU miners that speak KawPow stratum (T-Rex, lolMiner, GMiner, etc.)

### 2) Install dependencies

```bash
cd pool-gpu
npm install
```

### 3) Configure environment

Create a file (example name `gpu-pool.env`) — **do not commit secrets**:

```bash
export HOBC_RPC_URL=http://127.0.0.1:18762
export HOBC_RPC_USER=YOUR_RPC_USER
export HOBC_RPC_PASSWORD=YOUR_RPC_PASSWORD
export POOL_ADDRESS=hobc1q_your_pool_fallback_address
export HOBC_MARKER=1
export HOBC_POOL_ID=5
export HOBC_ALGO=1
export HOBC_POOL_SITE=your.pool.domain.example
export HOBC_POOL_NAME="Your GPU Pool Name"
export HOBC_CENSUS_TOKEN=YOUR_CENSUS_TOKEN_OR_EMPTY
export STRATUM_PORT=5558
```

Load it:

```bash
set -a
source ./gpu-pool.env
set +a
```

### 4) Start the pool

```bash
node hobc-mainnet-stratum-server.js
```

(Or the mainnet entry filename present in this folder — use the HobbyHash mainnet server script.)

### 5) Point a GPU miner

Example (T-Rex style):

```text
-a kawpow -o stratum+tcp://YOUR_SERVER:5558 -u hobc1qYourAddress.rig1 -p x
```

### 6) Verify

- Miner shows accepted shares
- `getblocktemplate` / jobs update when new tips arrive
- Solved blocks show **KawPow** and your pool label on the HobbyHash explorer
- Marker includes site / worker / rig when configured

---

## How the marker is built

`lib/hobc_marker.js` writes the same TLV layout as the SHA and CPU pools:

- magic `HOBC` + version
- `pool_id`, `algo`, `winning_share_diff`
- optional `pool_site`, `rig_model`, `worker_name`

The pool patches coinbase / merkle fields per authorized client so worker and rig identity can be unique per miner.

---

## Census

If `HOBC_CENSUS_TOKEN` is set, the server periodically calls node RPC `submitcensus` with KawPow hashrate and miner counts. IPs are **not** included.

---

## Support

HobbyHash Coin LLC — https://hobbyhashcoin.com
